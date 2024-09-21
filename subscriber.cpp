#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <netinet/tcp.h>

#include "common.h"
#include "helpers.h"

void run_client(int sockfd)
{
	int rc;
	char buf[MSG_MAXSIZE + 1];
	memset(buf, 0, MSG_MAXSIZE + 1);

	struct chat_packet sent_packet;
	struct chat_packet recv_packet;

	struct pollfd poll_fds[2];
	poll_fds[0].fd = sockfd;
	poll_fds[0].events = POLLIN;
	poll_fds[1].fd = STDIN_FILENO;
	poll_fds[1].events = POLLIN;

	while (1) {
		rc = poll(poll_fds, 2, -1);
		DIE(rc < 0, "poll");

		/* Get data on this socket. */
		if (poll_fds[0].revents & POLLIN) {
			rc = recv_all(poll_fds[0].fd, &recv_packet, sizeof(recv_packet));
			DIE(rc < 0, "recv");	

			/* Server is down. */
			if (rc == 0) {
				return;
			}

			std::cout << recv_packet.message;
		}

		if (poll_fds[1].revents & POLLIN) {
			fgets(buf, MSG_MAXSIZE, stdin);

			if (!strncmp(buf, "exit\n", strlen("exit\n"))) {
				rc = shutdown(sockfd, SHUT_RDWR);
				DIE(rc < 0, "error disconnect");
				return;
			} else {
				sent_packet.len = strlen(buf) + 1;
				strcpy(sent_packet.message, buf);

				/* Send packet to server. */	
				rc = send_all(sockfd, &sent_packet, sizeof(sent_packet));
				DIE(rc < 0, "send");
			}
		}
	}
}

int main(int argc, char *argv[]) {
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	int sockfd = -1;

	if (argc != 4) {
		printf("\n Usage: %s <id> <ip> <port>\n", argv[0]);
		return 1;
	}

	/* Extract port number. */
	uint16_t port;
	int rc = sscanf(argv[3], "%hu", &port);
	DIE(rc != 1, "Given port is invalid");

	/* Get a socket to connect to server. */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket");

	/* Make socket reusable and disable nagle algorithm. */
	int enable = 1, flag = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable,
		       sizeof(int)) < 0)
		perror("setsockopt(SO_REUSEADDR) failed");
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag,
				sizeof(int)) < 0)
		perror("setsockopt(TCP_NODELAY) failed");

	struct sockaddr_in serv_addr;
	socklen_t socket_len = sizeof(struct sockaddr_in);

	memset(&serv_addr, 0, socket_len);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	rc = inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr);
	DIE(rc <= 0, "inet_pton");

	/* Connect to server. */
	rc = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	DIE(rc < 0, "connect");

	/* Send id to server. */
	struct chat_packet send_id;
	strcpy(send_id.message, argv[1]);
	send_id.len = strlen(argv[1]) + 1;
	rc = send_all(sockfd, &send_id, sizeof(send_id));
	DIE(rc < 0, "send id");

	run_client(sockfd);

	/* Close connection. */
	close(sockfd);
	return 0;
}
