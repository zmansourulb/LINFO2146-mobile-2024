#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sys/node-id.h"
#include "net/packetbuf.h"
#include "commons.h"
#include "dev/serial-line.h"
#include "dev/uart0.h"


/*---------------------------------------------------------------------------*/

static int in_net = 1;
static m_rank_t rank = GATEWAY;
static linkaddr_t *children;
static int nb_children;
static linkaddr_t *dead_children;
static int nb_dead_children;

static struct ctimer timer;
static struct ctimer init_children_alive_timer;
static struct ctimer children_alive_timer;

static char* serv_token = "[2serv]";
static char* clie_token = "[2clie]";

/*---------------------------------------------------------------------------*/
PROCESS(gateway_process, "Gateway process");
AUTOSTART_PROCESSES(&gateway_process);
/*---------------------------------------------------------------------------*/

static void send_hello_message(void* ptr) {
  ctimer_reset(&timer);
  m_packet_t msg = encode_message(GATEWAY, HELLO);
  nullnet_buf = (uint8_t *)(&msg);
  nullnet_len = sizeof(m_packet_t);
  NETSTACK_NETWORK.output(NULL);
}

static void check_children_alive(void* ptr) {
  for (int i = 0; i < nb_dead_children; i++) {
    for (int j = 0; j < nb_children; j++) {
      if (linkaddr_cmp(&dead_children[i], &children[j]) != 0) {
        nb_children = remove_linkaddr(&children, nb_children, &children[j]);
        j--;
      }
    }
  }
  free(dead_children);
  ctimer_reset(&init_children_alive_timer);
}

static void init_check_children_alive(void* ptr) {
  nb_dead_children = nb_children;
  dead_children = (linkaddr_t*) malloc(nb_dead_children * sizeof(linkaddr_t));
  for (int i = 0; i < nb_dead_children; i++) {
    dead_children[i] = children[i];
  }
  memcpy(dead_children, children, nb_children);
  ctimer_set(&children_alive_timer, ALIVE_TIMEOUT_INTERVAL, check_children_alive, NULL);
}

void parse_string(char* str, m_rank_t* rank, m_msgcat_t* msgcat, m_appcat_t* appcat, int* value, linkaddr_t* src) {
  char* token;
  char* endptr;
  char temp_str[25];
  strncpy(temp_str, str, sizeof(temp_str)-1);
  temp_str[sizeof(temp_str)-1] = '\0';
  
  token = strtok(temp_str, "|");
  if (token != NULL) *rank = strtol(token, &endptr, 10);
  token = strtok(NULL, "|");
  if (token != NULL) *msgcat = strtol(token, &endptr, 10);
  token = strtok(NULL, "|");
  if (token != NULL) *appcat = strtol(token, &endptr, 10);
  token = strtok(NULL, "|");
  if (token != NULL) *value = strtol(token, &endptr, 10);
  token = strtok(NULL, "|");
  if (token != NULL) {
    for (int i = 0; i < LINKADDR_SIZE; i++) {
      char hex_byte[3];
      hex_byte[0] = token[2 * i];
      hex_byte[1] = token[2 * i + 1];
      hex_byte[2] = '\0';
      src->u8[i] = (unsigned char)strtol(hex_byte, NULL, 16);
    }
  }
}

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  m_packet_t dmsg = *(m_packet_t*) data;

  if (dmsg.msgcat == HELLO) {
    // remove child from dead children if it's alive
    nb_dead_children = remove_linkaddr(&dead_children, nb_dead_children, src);
  }

  if (dmsg.msgcat == HELLO_ACK) {
    nb_children = add_linkaddr(&children, nb_children, src);
    linkaddr_t src_copy;
    linkaddr_copy(&src_copy, src);
  }

  else if (dmsg.msgcat == CHILD_DISCONNECT) {
    nb_children = remove_linkaddr(&children, nb_children, src);
  }

  else if (dmsg.msgcat == APPLICATION) {
    if (dmsg.appcat == APP_LGT_LVL) {
      linkaddr_copy(&dmsg.src, src); // simple NAT
      printf("%s", serv_token);
      printf("{\"rank\":%d,", dmsg.rank);
      printf("\"msgcat\":%d,", dmsg.msgcat);
      printf("\"appcat\":%d,", dmsg.appcat);
      printf("\"value\":%d,", dmsg.value);
      printf("\"src\":\"%02u%02u.%02u%02u.%02u%02u.%02u%02u\"}\n", src->u8[0], src->u8[1], src->u8[2], src->u8[3], src->u8[4], src->u8[5], src->u8[6], src->u8[7]);
    } else if (dmsg.appcat == APP_IRG_ACK) {
      printf("%s", serv_token);
      printf("{\"rank\":%d,", dmsg.rank);
      printf("\"msgcat\":%d,", dmsg.msgcat);
      printf("\"appcat\":%d,", dmsg.appcat);
      printf("\"value\":%d,", dmsg.value);
      printf("\"src\":\"%02u%02u.%02u%02u.%02u%02u.%02u%02u\"}\n", dmsg.src.u8[0], dmsg.src.u8[1], dmsg.src.u8[2], dmsg.src.u8[3], dmsg.src.u8[4], dmsg.src.u8[5], dmsg.src.u8[6], dmsg.src.u8[7]);
    }
  }
}

PROCESS_THREAD(gateway_process, ev, data) {

  PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  nullnet_set_input_callback(input_callback);
 
  ctimer_set(&timer, SEND_INTERVAL, send_hello_message, NULL);
  ctimer_set(&init_children_alive_timer, CLOCK_SECOND, init_check_children_alive, NULL);

  update_mote_color(in_net, rank, NO_CAT);

  serial_line_init();
  uart0_set_input(serial_line_input_byte);

  while(1) {
    PROCESS_YIELD();
    if(ev == serial_line_event_message) {
      if (strncmp((char*) data, clie_token, 7) == 0) {
        char *input_string = &((char*) data)[7];
        m_rank_t rank;
        m_msgcat_t msgcat;
        m_appcat_t appcat;
        int value;
        linkaddr_t src;
        parse_string(input_string, &rank, &msgcat, &appcat, &value, &src);
        if (msgcat == APPLICATION) {
          if (appcat == APP_LGT_ON) {
            m_packet_t msg = encode_app_message(GATEWAY, appcat, value);
            nullnet_buf = (uint8_t *)(&msg);
            nullnet_len = sizeof(m_packet_t);
            NETSTACK_NETWORK.output(&src);
          } else if (appcat == APP_IRG_ON) {
            for (int i = 0; i < nb_children; i++) {
              m_packet_t msg = encode_app_message(GATEWAY, appcat, value);
              nullnet_buf = (uint8_t *)(&msg);
              nullnet_len = sizeof(m_packet_t);
              NETSTACK_NETWORK.output(&children[i]);
            }
          }
        }
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/