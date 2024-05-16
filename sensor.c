#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include <string.h>
#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include <limits.h>
#include "sys/node-id.h"
#include "net/packetbuf.h"
#include "commons.h"
#include "dev/uart0.h"
#include "dev/leds.h"

#define SERIAL_BUF_SIZE 128

/*---------------------------------------------------------------------------*/

static const linkaddr_t null_parent = {{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};

static int in_net = 0;
static linkaddr_t parent = {{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};
static int parent_strength = INT_MIN;
static int parent_rank = INT_MAX;
static m_rank_t rank = SENSOR;
static linkaddr_t *children;
static int nb_children;
static linkaddr_t *dead_children;
static int nb_dead_children;
static m_sensor_t sensor_cat = NO_CAT;

static struct ctimer parent_alive_timeout_timer;
static struct ctimer init_children_alive_timer;
static struct ctimer children_alive_timer;
static struct ctimer send_hello_timer;

static struct ctimer app_message_timer;
static struct ctimer light_off_timer;
static struct ctimer irrigation_off_timer;

/*---------------------------------------------------------------------------*/
PROCESS(sensor_process, "Sensor process");
AUTOSTART_PROCESSES(&sensor_process);
/*---------------------------------------------------------------------------*/

static void set_irrigation_off(void *ptr) {
  leds_off(LEDS_GREEN);
  m_packet_t msg = encode_app_message(SENSOR, APP_IRG_ACK, 0);
  linkaddr_copy(&msg.src, &linkaddr_node_addr);
  nullnet_buf = (uint8_t *)(&msg);
  nullnet_len = sizeof(m_packet_t);
  NETSTACK_NETWORK.output(&parent);
}

static void set_light_off(void *ptr) {
  leds_off(LEDS_GREEN);
}

void send_light_level() {
  int light_level = rand() % 100;
  light_level = light_level < 0 ? -light_level : light_level;
  LOG_INFO("Light level: %d\n", light_level);
  m_packet_t msg = encode_app_message(SENSOR, APP_LGT_LVL, light_level);
  nullnet_buf = (uint8_t *)(&msg);
  nullnet_len = sizeof(m_packet_t);
  NETSTACK_NETWORK.output(&parent);
}

void interact_with_light_sensor() {
  for (int i = 0; i < 5; i++) {
    m_packet_t msg = encode_app_message(SENSOR, APP_MOB_LGT_SEN, 0);
    nullnet_buf = (uint8_t *)(&msg);
    nullnet_len = sizeof(m_packet_t);
    NETSTACK_NETWORK.output(&parent);
    LOG_INFO("Mobile terminal sent a message to the light sensor...\n");
  }
}

static void send_app_message(void *ptr) {
  ctimer_reset(&app_message_timer);
  if (in_net) {
    if (sensor_cat == LGT_SEN) {
      send_light_level();
    } else if (sensor_cat == MOB_TER) {
      interact_with_light_sensor();
    }
  }
}

static void send_hello_message(void *ptr) {
  m_packet_t msg = encode_message(SENSOR, HELLO);
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
  LOG_INFO("Node has %d children:\n", nb_children);
  for (int i = 0; i < nb_children; i++) {
    LOG_INFO("> %02u%02u.%02u%02u.%02u%02u.%02u%02u\n", children[i].u8[0], children[i].u8[1], children[i].u8[2], children[i].u8[3], children[i].u8[4], children[i].u8[5], children[i].u8[6], children[i].u8[7]);
  }
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

static void parent_alive_timeout(void* ptr) {
  in_net = 0;
  parent = null_parent;
  update_mote_color(in_net, rank, sensor_cat);
  LOG_INFO("Timeout: No response received, detaching from parent\n");
}

void set_parent(const linkaddr_t* src, m_rank_t msgrank, int strength) {
  in_net = 1;
  parent_rank = msgrank;
  parent_strength = strength;
  linkaddr_copy(&parent, src);
  LOG_INFO("Set %02u%02u.%02u%02u.%02u%02u.%02u%02u as parent\n", parent.u8[0], parent.u8[1], parent.u8[2], parent.u8[3], parent.u8[4], parent.u8[5], parent.u8[6], parent.u8[7]);
  update_mote_color(in_net, rank, sensor_cat);
  ctimer_set(&parent_alive_timeout_timer, ALIVE_TIMEOUT_INTERVAL, parent_alive_timeout, NULL);
}

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  int strength = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  m_packet_t dmsg = *(m_packet_t*) data;

  if (dmsg.msgcat == HELLO) {
    if (
        (!in_net || (in_net && (dmsg.rank < parent_rank || (dmsg.rank == parent_rank && strength > parent_strength))))
      && dmsg.rank != GATEWAY
      && find_linkaddr(&children, nb_children, src) == -1 // potential parent not in the children
    ) {
      linkaddr_t old_parent = parent;
      set_parent(src, dmsg.rank, strength);
      ctimer_set(&send_hello_timer, CLOCK_SECOND, send_hello_message, NULL);
      LOG_INFO("Node in network\n");
      m_packet_t msg = encode_message(SENSOR, HELLO_ACK);
      nullnet_buf = (uint8_t *)(&msg);
      nullnet_len = sizeof(m_packet_t);
      NETSTACK_NETWORK.output(&parent);
      if (linkaddr_cmp(&old_parent, &null_parent) == 0 && linkaddr_cmp(&old_parent, &parent) == 0) {
        m_packet_t msg = encode_message(SENSOR, CHILD_DISCONNECT);
        nullnet_buf = (uint8_t *)(&msg);
        nullnet_len = sizeof(m_packet_t);
        NETSTACK_NETWORK.output(&old_parent);
      }
    } else {
      if (linkaddr_cmp(src, &parent) != 0) {
        // can only receive HELLO from the parent to stay in the net
        ctimer_set(&send_hello_timer, CLOCK_SECOND, send_hello_message, NULL);
        ctimer_restart(&parent_alive_timeout_timer);
      } else {
        // remove child from dead children if it's alive
        nb_dead_children = remove_linkaddr(&dead_children, nb_dead_children, src);
      }
    }
  }

  else if (dmsg.msgcat == HELLO_ACK) {
    if (find_linkaddr(&children, nb_children, src) == -1) { // potential child not parent
      nb_children = add_linkaddr(&children, nb_children, src);
      LOG_INFO("Node has %d children:\n", nb_children);
      for (int i = 0; i < nb_children; i++) {
        LOG_INFO("> %02u%02u.%02u%02u.%02u%02u.%02u%02u\n", children[i].u8[0], children[i].u8[1], children[i].u8[2], children[i].u8[3], children[i].u8[4], children[i].u8[5], children[i].u8[6], children[i].u8[7]);
      }
      linkaddr_t src_copy;
      linkaddr_copy(&src_copy, src);
    }
  }

  else if (dmsg.msgcat == CHILD_DISCONNECT) {
    nb_children = remove_linkaddr(&children, nb_children, src);
    LOG_INFO("Node has %d children:\n", nb_children);
    for (int i = 0; i < nb_children; i++) {
      LOG_INFO("> %02u%02u.%02u%02u.%02u%02u.%02u%02u\n", children[i].u8[0], children[i].u8[1], children[i].u8[2], children[i].u8[3], children[i].u8[4], children[i].u8[5], children[i].u8[6], children[i].u8[7]);
    }
  }

  else if (dmsg.msgcat == NULL_MSG);

  else if (dmsg.msgcat == APPLICATION) {
    if (dmsg.appcat == APP_LGT_LVL) {
      nullnet_buf = (uint8_t *)(&dmsg);
      nullnet_len = sizeof(m_packet_t);
      NETSTACK_NETWORK.output(&parent);
    } else if (dmsg.appcat == APP_LGT_ON) {
      if (sensor_cat == LGT_BLB) {
        leds_on(LEDS_GREEN);
        ctimer_set(&light_off_timer, dmsg.value * CLOCK_SECOND, set_light_off, NULL);
      }
      for (int i = 0; i < nb_children; i++) {
        nullnet_buf = (uint8_t *)(&dmsg);
        nullnet_len = sizeof(m_packet_t);
        NETSTACK_NETWORK.output(&children[i]);
      }
    } else if (dmsg.appcat == APP_IRG_ON) {
      if (sensor_cat == IRG_SYS) {
        leds_on(LEDS_GREEN);
        m_packet_t msg = encode_app_message(SENSOR, APP_IRG_ACK, 1);
        linkaddr_copy(&msg.src, &linkaddr_node_addr);
        nullnet_buf = (uint8_t *)(&msg);
        nullnet_len = sizeof(m_packet_t);
        NETSTACK_NETWORK.output(&parent);
        ctimer_set(&irrigation_off_timer, dmsg.value * CLOCK_SECOND, set_irrigation_off, NULL);
      }
      for (int i = 0; i < nb_children; i++) {
        nullnet_buf = (uint8_t *)(&dmsg);
        nullnet_len = sizeof(m_packet_t);
        NETSTACK_NETWORK.output(&children[i]);
      }
    } else if (dmsg.appcat == APP_IRG_ACK) {
      nullnet_buf = (uint8_t *)(&dmsg);
      nullnet_len = sizeof(m_packet_t);
      NETSTACK_NETWORK.output(&parent);
    } else if (dmsg.appcat == APP_MOB_LGT_SEN) {
      if (dmsg.value % 2 == 0) {
        // mobile terminal -> subgateway (-> light sensor)
        nullnet_buf = (uint8_t *)(&dmsg);
        nullnet_len = sizeof(m_packet_t);
        NETSTACK_NETWORK.output(&parent);
      } else {
        if (dmsg.value == 1 && sensor_cat == LGT_SEN) {
          dmsg.value++;
          nullnet_buf = (uint8_t *)(&dmsg);
          nullnet_len = sizeof(m_packet_t);
          NETSTACK_NETWORK.output(&parent);
        } else if (dmsg.value == 3 && sensor_cat == MOB_TER) {
          LOG_INFO("Mobile terminal got a response from the light sensor...\n");
        } else {
          // (mobile terminal ->) subgateway -> light sensor
          for (int i = 0; i < nb_children; i++) {
            nullnet_buf = (uint8_t *)(&dmsg);
            nullnet_len = sizeof(m_packet_t);
            NETSTACK_NETWORK.output(&children[i]);
          }
        }
      }
    }
  }

  else {
    LOG_INFO("/!\\ Message answer not yet implemented: %d\n", dmsg.msgcat);
  }

}

static int uart_rx_callback(unsigned char c) {
  if (c == 'a') {
    sensor_cat = IRG_SYS;
    LOG_INFO("Set sensor to %d", IRG_SYS);
  }
  else if (c == 'b') {
    sensor_cat = MOB_TER;
    LOG_INFO("Set sensor to %d", MOB_TER);
  }
  else if (c == 'c') {
    sensor_cat = LGT_SEN;
    LOG_INFO("Set sensor to %d", LGT_SEN);
  }
  else if (c == 'd') {
    sensor_cat = LGT_BLB;
    LOG_INFO("Set sensor to %d", LGT_BLB);
  }
  update_mote_color(in_net, rank, sensor_cat);
  return 0;
}

PROCESS_THREAD(sensor_process, ev, data) {

  PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  nullnet_set_input_callback(input_callback);

  uart0_init(BAUD2UBR(115200)); //set the baud rate as necessary
  uart0_set_input(uart_rx_callback); //set the callback function

  ctimer_set(&init_children_alive_timer, CLOCK_SECOND, init_check_children_alive, NULL);
  ctimer_set(&app_message_timer, 5 * CLOCK_SECOND, send_app_message, NULL);

  // Initialize random
  srand(clock_time());

  update_mote_color(in_net, rank, sensor_cat);
  
  // Needed it init, otherwise the mote will never receive any message  
  NETSTACK_NETWORK.output(NULL);


  while(1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/