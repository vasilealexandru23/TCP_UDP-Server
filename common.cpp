#include "common.h"

#include <sys/socket.h>
#include <sys/types.h>

int recv_all(int sockfd, void *buffer, size_t len)
{
	size_t bytes_recv = 0;
	size_t bytes_remaining = len;
	char *buff = (char *)buffer;

	while (bytes_remaining > 0) {
		bytes_recv = recv(sockfd, buff + bytes_recv, bytes_remaining, 0);
		if (bytes_recv == 0) {
			break;
		}
		bytes_remaining -= bytes_recv;
	}

	return len - bytes_remaining;
}

int send_all(int sockfd, void *buffer, size_t len)
{
	size_t bytes_sent = 0;
	size_t bytes_remaining = len;
	char *buff = (char *)buffer;

	while (bytes_remaining > 0) {
		bytes_sent = send(sockfd, buff + bytes_sent, bytes_remaining, 0);
		if (bytes_sent == 0) break;
		bytes_remaining -= bytes_sent;
	}

	return len - bytes_remaining;
}
