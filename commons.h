#ifndef COMMONS_H
#define COMMONS_H
#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include "net/linkaddr.h"
#include "sys/log.h"
#include "sys/clock.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
#define SEND_INTERVAL (10 * CLOCK_SECOND)
#define ALIVE_TIMEOUT_INTERVAL (20 * CLOCK_SECOND)
#define PACKET_LENGTH

#if MAC_CONF_WITH_TSCH
#include "net/mac/tsch/tsch.h"
static linkaddr_t coordinator_addr =  {{ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};
#endif /* MAC_CONF_WITH_TSCH */


typedef enum m_rank { GATEWAY, SUBGATEWAY, SENSOR } m_rank_t;

typedef enum m_msgcat { NULL_MSG, HELLO, HELLO_ACK, CHILD_DISCONNECT, APPLICATION } m_msgcat_t;

typedef enum m_appcat { NULL_APP, APP_LGT_LVL, APP_LGT_ON, APP_IRG_ON, APP_IRG_ACK, APP_MOB_LGT_SEN } m_appcat_t;

typedef enum m_sensor { NO_CAT, IRG_SYS, MOB_TER, LGT_SEN, LGT_BLB } m_sensor_t;

typedef struct m_packet {
    m_rank_t rank;
    m_msgcat_t msgcat;
    m_appcat_t appcat;
    int value;
    linkaddr_t src;
} m_packet_t;

m_packet_t encode_message(m_rank_t rank, m_msgcat_t msgcat);

m_packet_t encode_app_message(m_rank_t rank, m_appcat_t appcat, int value);

void decode_message(int msg, int* out);

void update_mote_color(int in_net, m_rank_t rank, m_sensor_t sensor_cat);

size_t getsize(void *p);

void *realloc(void *ptr, size_t size);

int add_linkaddr(linkaddr_t **set_ptr, int size, const linkaddr_t *item);

int find_linkaddr(linkaddr_t **set_ptr, int size, const linkaddr_t *item);

int remove_linkaddr(linkaddr_t **set_ptr, int size, const linkaddr_t *item);

#endif /* COMMONS_H */