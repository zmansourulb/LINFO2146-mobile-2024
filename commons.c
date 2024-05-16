#include "commons.h"
#include <string.h>


m_packet_t encode_message(m_rank_t rank, m_msgcat_t msgcat) {
  m_packet_t packet = {
    .rank = rank,
    .msgcat = msgcat,
    .appcat = NULL_APP,
    .value = 0,
    .src={{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},
  };
  return packet;
}

m_packet_t encode_app_message(m_rank_t rank, m_appcat_t appcat, int value)   {
  m_packet_t packet = {
    .rank = rank,
    .msgcat = APPLICATION,
    .appcat = appcat,
    .value = value,
    .src={{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},
  };
  return packet;
}

void update_mote_color(int in_net, m_rank_t rank, m_sensor_t sensor_cat) {
  if (rank == GATEWAY) { // GREY
    if (in_net)
      printf("#A color=#888888\n");
    else
      printf("#A color=#444444\n");
  } else if (rank == SUBGATEWAY) { // PURPLE
    if (in_net)
      printf("#A color=#8a10ff\n");
    else
      printf("#A color=#c289f9\n");
  } else {
    if (sensor_cat == IRG_SYS) { // BLUE
      if (in_net)
        printf("#A color=#00d4ff\n");
      else
        printf("#A color=#b3ebf6\n");
    }
    else if (sensor_cat == MOB_TER) { // RED
      if (in_net)
        printf("#A color=#e61142\n");
      else
        printf("#A color=#f7a4b7\n");
    }
    else if (sensor_cat == LGT_BLB) { // YELLOW
      if (in_net)
        printf("#A color=#f7ff0b\n");
      else
        printf("#A color=#f2f4c0\n");
    }
    else if (sensor_cat == LGT_SEN) { // ORANGE
      if (in_net)
        printf("#A color=#f59e0c\n");
      else
        printf("#A color=#f4d39c\n");
    }
  } 
}

size_t getsize(void *p) {
	size_t *in = p;
	if(in) {
		--in; 
		return *in;
	}
	return -1;
}

void *realloc(void *ptr, size_t size) {
	void *newptr;
	int msize;
	msize = getsize(ptr);

	if (size <= msize) {
		return ptr;
  }

	newptr = malloc(size);
	memcpy(newptr, ptr, msize);
	free(ptr);

	return newptr;
}

int add_linkaddr(linkaddr_t **set_ptr, int size, const linkaddr_t *item) {
  linkaddr_t *set = *set_ptr;
  if (size == 0) {
    set = (linkaddr_t*) malloc(sizeof(linkaddr_t));
  }

  for (int i = 0; i < size; i++) {
    if (linkaddr_cmp(&set[i], item) != 0)
      // item already added
      return size;
  }

  set = (linkaddr_t*) realloc(set, (size + 1) * sizeof(linkaddr_t));
  linkaddr_copy(&set[size], item);
  *set_ptr = set;
  
  return ++size;
}

int find_linkaddr(linkaddr_t **set_ptr, int size, const linkaddr_t *item) {
  linkaddr_t *set = *set_ptr;

  for (int i = 0; i < size; i++) {
    if (linkaddr_cmp(&set[i], item) != 0) {
      return i;
    }
  }

  return -1;
}

int remove_linkaddr(linkaddr_t **set_ptr, int size, const linkaddr_t *item) {
  linkaddr_t *set = *set_ptr;
  
  int pos = find_linkaddr(set_ptr, size, item);

  if (pos == -1) {
    // element not found
    return size;
  }

  set = (linkaddr_t*) realloc(set, (size - 1) * sizeof(linkaddr_t));
  for (int i = pos; i < size - 1; i++) {
    set[i] = set[i + 1];
  }
  *set_ptr = set;

  return --size;
}