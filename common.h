#ifndef __COMMON_H__
#define __COMMON_H__

#include <stddef.h>
#include <stdint.h>

int send_all(int sockfd, void *buff, size_t len);
int recv_all(int sockfd, void *buff, size_t len);

/* Dimensiunea maxima a mesajului */
#define MSG_MAXSIZE 1700

struct chat_packet {
	uint16_t len;
	char message[MSG_MAXSIZE + 1];
};

#define TOPIC_MAXSIZE 50
#define PAYLOAD_MAXSIZE 1500

struct __attribute__((__packed__)) udp_packet {
		char topic[TOPIC_MAXSIZE];
		uint8_t type;
		char payload[PAYLOAD_MAXSIZE];
};

#endif
