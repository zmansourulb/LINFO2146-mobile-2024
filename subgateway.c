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

/*---------------------------------------------------------------------------*/

static const linkaddr_t null_parent = {{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};

static int in_net = 0;
static linkaddr_t parent = {{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};
static int parent_strength = INT_MIN;
static int parent_rank = INT_MAX;
static m_rank_t rank = SUBGATEWAY;
static linkaddr_t *children;
static int nb_children;
static linkaddr_t *dead_children;
static int nb_dead_children;

static struct ctimer parent_alive_timeout_timer;
static struct ctimer init_children_alive_timer;
static struct ctimer children_alive_timer;
static struct ctimer send_hello_timer;

/*---------------------------------------------------------------------------*/
PROCESS(subgateway_process, "Subgateway process");
AUTOSTART_PROCESSES(&subgateway_process);
/*---------------------------------------------------------------------------*/

static void send_hello_message(void *ptr) {
  m_packet_t msg = encode_message(SUBGATEWAY, HELLO);
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
  update_mote_color(in_net, rank, NO_CAT);
  LOG_INFO("Timeout: No response received, detaching from parent\n");
}

void set_parent(const linkaddr_t* src, m_rank_t msgrank, int strength) {
  in_net = 1;
  parent_rank = msgrank;
  parent_strength = strength;
  linkaddr_copy(&parent, src);
  LOG_INFO("Set %02u%02u.%02u%02u.%02u%02u.%02u%02u as parent (%d)\n", parent.u8[0], parent.u8[1], parent.u8[2], parent.u8[3], parent.u8[4], parent.u8[5], parent.u8[6], parent.u8[7], msgrank);
  update_mote_color(in_net, rank, NO_CAT);
  ctimer_set(&parent_alive_timeout_timer, ALIVE_TIMEOUT_INTERVAL, parent_alive_timeout, NULL);
}

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
  int strength = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  m_packet_t dmsg = *(m_packet_t*) data;

  if (dmsg.msgcat == HELLO) {
    if (!in_net && dmsg.rank == GATEWAY) {
      linkaddr_t old_parent = parent;
      set_parent(src, dmsg.rank, strength);
      ctimer_set(&send_hello_timer, CLOCK_SECOND, send_hello_message, NULL);
      LOG_INFO("Node in network\n");
      m_packet_t msg = encode_message(SUBGATEWAY, HELLO_ACK);
      nullnet_buf = (uint8_t *)(&msg);
      nullnet_len = sizeof(m_packet_t);
      NETSTACK_NETWORK.output(&parent);
      if (linkaddr_cmp(&old_parent, &null_parent) == 0 && linkaddr_cmp(&old_parent, &parent) == 0) {
        m_packet_t msg = encode_message(SUBGATEWAY, CHILD_DISCONNECT);
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
    nb_children = add_linkaddr(&children, nb_children, src);
    LOG_INFO("Node has %d children:\n", nb_children);
    for (int i = 0; i < nb_children; i++) {
      LOG_INFO("> %02u%02u.%02u%02u.%02u%02u.%02u%02u\n", children[i].u8[0], children[i].u8[1], children[i].u8[2], children[i].u8[3], children[i].u8[4], children[i].u8[5], children[i].u8[6], children[i].u8[7]);
    }
    linkaddr_t src_copy;
    linkaddr_copy(&src_copy, src);
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
    } else if (dmsg.appcat == APP_LGT_ON || dmsg.appcat == APP_IRG_ON) {
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
      dmsg.value++;
      for (int i = 0; i < nb_children; i++) {
        nullnet_buf = (uint8_t *)(&dmsg);
        nullnet_len = sizeof(m_packet_t);
        NETSTACK_NETWORK.output(&children[i]);
      }
    }
  }

  else {
    LOG_INFO("/!\\ Message answer not yet implemented: %d\n", dmsg.msgcat);
  }

}

PROCESS_THREAD(subgateway_process, ev, data) {

  PROCESS_BEGIN();

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  nullnet_set_input_callback(input_callback);

  ctimer_set(&init_children_alive_timer, CLOCK_SECOND, init_check_children_alive, NULL);

  update_mote_color(in_net, rank, NO_CAT);
  
  // Needed it init, otherwise the mote will never receive any message  
  NETSTACK_NETWORK.output(NULL);


  while(1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/