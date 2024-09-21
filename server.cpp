#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <netinet/tcp.h>

#include "common.h"
#include "helpers.h"
#include <cmath>

#define MAX_CONNECTIONS 1024

bool match_topics(std::vector<std::string> my_topic,
		std::vector<std::string> req_topic) {
	size_t iter0 = 0; 	/* my_topic */
	size_t iter1 = 0;	/* req_topic */

	while(true) {
		if (iter0 == my_topic.size() || iter1 == req_topic.size()) {
			if (iter0 == my_topic.size() && iter1 == req_topic.size()) {
				return true;
			}
			return false;
		}
		if (my_topic[iter0] == req_topic[iter1]) {
			iter0++, iter1++;
			continue;
		} if (my_topic[iter0] == "+") {
			iter0++, iter1++;
			continue;
		} else if (my_topic[iter0] == "*") {
			if (iter0 == my_topic.size() - 1) {
				return true;
			} else {
				std::string next_match = my_topic[iter0 + 1];
				for (size_t j = iter1; j < req_topic.size(); ++j) {
					if (req_topic[j] == next_match) {
						std::vector<std::string> next_my_topic;
						std::vector<std::string> next_req_topic;

						for (size_t k = j; k < req_topic.size(); ++k)
							next_req_topic.push_back(req_topic[k]);
						for (size_t k = iter0 + 1; k < my_topic.size(); ++k)
							next_my_topic.push_back(my_topic[k]);

						if (match_topics(next_my_topic, next_req_topic)) {
							return true;
						}
					}
				}
				return false;
			}
		} else {
			return false;
		}
	}

	return true;
}

bool matched_subs(std::vector<std::vector<std::string>> &subscribed_topics,
		std::vector<std::string> &udp_topic_tokens)
{
	for (size_t i = 0; i < subscribed_topics.size(); ++i) {
		std::vector<std::string> curr_topic;

		/* Erase first element, because first element is the full subscribe. */
		for (size_t j = 1; j < subscribed_topics[i].size(); ++j) {
			curr_topic.push_back(subscribed_topics[i][j]);
		}

		if (match_topics(curr_topic, udp_topic_tokens)) {
			return true;
		}
	}

	return false;
}

void send_subscribers(std::vector<struct pollfd> &poll_fds,
		std::unordered_map<int, std::string> &id_clients,
		std::unordered_map<std::string, std::vector<std::vector<std::string>>>
		&map_id_topics, std::vector<std::string> &udp_topic_tokens,
		struct chat_packet &send_updates)
{
	int rc;

	for (size_t j = 3; j < poll_fds.size(); ++j) {
		if (matched_subs(map_id_topics[id_clients[poll_fds[j].fd]],
				 udp_topic_tokens)) {
			rc = send_all(poll_fds[j].fd, &send_updates,
					  sizeof(struct chat_packet));
			DIE(rc < 0, "send");
		}
	}
}

std::string extract_data(struct sockaddr_in cli_addr,
			 struct udp_packet udp_recv)
{
	std::string message;

	/* Add address and port of source. */
	message = std::string(inet_ntoa(cli_addr.sin_addr)) + ":" +
		  std::to_string(ntohs(cli_addr.sin_port));

	/* Add topic. */
	message += " - " + std::string(udp_recv.topic) + " - ";

	/* Based on type of payload, add to message data. */
	if (udp_recv.type == (uint8_t)0) {
		message += "INT - ";
		std::string sign = (*(uint8_t *)udp_recv.payload == 0 ? "" : "-");
		uint32_t integer = ntohl(*(uint32_t *)((udp_recv.payload) + 1));

		if (integer == 0) {
			message += std::to_string(integer);
		} else {
			message += sign + std::to_string(integer);
		}
	} else if (udp_recv.type == (uint8_t)1) {
		message += "SHORT_REAL - ";
		message += std::to_string(ntohs(*(uint16_t *)udp_recv.payload) / 100.0);
	} else if (udp_recv.type == (uint8_t)2) {
		message += "FLOAT - ";
		float real = ntohl(*(uint32_t *)((uint8_t *)udp_recv.payload + 1));
		uint8_t fract = *((uint8_t *)udp_recv.payload + 5);

		if (*(uint8_t *)udp_recv.payload == 0) {
			message += std::to_string(real / pow(10, fract));
		} else {
			message += std::to_string(-real / pow(10, fract));
		}
	} else if (udp_recv.type == (uint8_t)3) {
		message += "STRING - " + std::string(udp_recv.payload);
	}

	return message;
}

void prepare_data(struct sockaddr_in cli_addr,
		struct udp_packet udp_client_recv, struct chat_packet *send_updates,
		std::vector<std::string> &udp_topic_tokens)
{
	memset(send_updates, 0, sizeof(struct chat_packet));

	std::string message = extract_data(cli_addr, udp_client_recv);

	size_t len = 0;
	for (len = 0; len < message.size() && len < PAYLOAD_MAXSIZE; ++len) {
		send_updates->message[len] = message[len];
	}

	/* Convert char * to string. */
	std::string topic_str;
	for (int j = 0; udp_client_recv.topic[j] != '\0'
					&& j < TOPIC_MAXSIZE; ++j) {
		topic_str.push_back(udp_client_recv.topic[j]);
	}

	/* Break topic in mulitple messages. */
	std::istringstream iss(topic_str);
	std::string delim;

	while (std::getline(iss, delim, '/')) {
		udp_topic_tokens.push_back(delim);
	}

	send_updates->message[len] = '\n';
	send_updates->message[len + 1] = '\0';
	send_updates->len = len + 1;
}

void new_tcp_connection(int listenTCP, std::vector<struct pollfd> &poll_fds,
			std::unordered_set<int> &waiting_id,
			std::unordered_map<int, struct sockaddr_in> &client_data) {
	/* New request connection. */
	struct sockaddr_in cli_addr;
	socklen_t socket_len = sizeof(struct sockaddr_in);

	/* Accept new client. */
	int newsockfd = accept(listenTCP,
				(struct sockaddr *)&cli_addr, &socket_len);
	DIE(newsockfd < 0, "accept");

	/* Add new client to waiting list. */
	waiting_id.insert(newsockfd);
	client_data[newsockfd] = cli_addr;

	/* Add new client to poll_fds. */
	struct pollfd new_poll = {newsockfd, POLLIN, 0};
	poll_fds.push_back(new_poll);
}

bool input_server(std::vector<struct pollfd> &poll_fds)
{
	std::string input;

	/* Read input. */
	std::cin >> input;

	/* React only to exit command. */
	if (input == "exit") {
		for (size_t i = 0; i < poll_fds.size(); ++i) {
			shutdown(poll_fds[i].fd, SHUT_RDWR);
			close(poll_fds[i].fd);
		}
		return true;
	}

	/* Ignore input otherwise. */
	return false;
}

void remove_client(int idx, std::vector<struct pollfd> &poll_fds)
{
	poll_fds.erase(poll_fds.begin() + idx);
}

void disconnect_client(int client_sockfd, std::vector<struct pollfd> &poll_fds,
			  int idx_poll, std::unordered_map<int, std::string> &id_clients,
			  std::unordered_set<std::string> &used_id_clients) {
	
	std::cout << "Client " + id_clients[client_sockfd] + " disconnected.\n";

	close(client_sockfd);
	used_id_clients.erase(id_clients[client_sockfd]);

	/* Remove client from poll. */
	remove_client(idx_poll, poll_fds);	
}

void process_client(char *recv_message, int cli_sockfd, std::string id_cli,
	std::unordered_map<std::string, std::vector<std::vector<std::string>>> 
	&map_id_topics)
{
	int rc;
	char *p;
	bool cmd;
	std::string feedback;
	struct chat_packet send_feedback;	

	/* Subscribe/Unsubcribe command. */
	p = strtok(recv_message, " ");

	if (!strcmp(p, "subscribe")) {
		feedback = "Subscribed to topic ";
		cmd = true;
	} else if (!strcmp(p, "unsubscribe")) {
		feedback = "Unsubscribed from topic ";
		cmd = false;
	} else {
		/* Ignore other command. */
		return;
	}

	/* Extract topic and convert to string. */
	p = strtok(NULL, " ");

	std::string topic;
	/* Received message always has '\n'. */
	for (int j = 0; p[j] != '\n'; j++) {
		topic.push_back(p[j]);
	}

	/* Add to message the topic. */
	feedback += topic + "\n";

	if (cmd == true) {
		/* Vector containing whole topic
	   	   path and delimitated sub-topics. */
		std::vector<std::string> topic_tokens;
		topic_tokens.push_back(topic);

		/* Extract sub-topics. */
		std::istringstream iss(topic);
		std::string delim;
		while (std::getline(iss, delim, '/')) {
			topic_tokens.push_back(delim);
		}

		map_id_topics[id_cli].push_back(topic_tokens);
	} else {
		for (size_t j = 0; j < map_id_topics[id_cli].size(); ++j) {
			if (map_id_topics[id_cli][j][0] == topic) {
				/* Erase this vector. */
				map_id_topics[id_cli].erase(map_id_topics[id_cli].begin() + j);
				break;
			}
		}
	}

	/* Prepare feedback packet to client. */
	memset(&send_feedback, 0, sizeof(struct chat_packet));
	strcpy(send_feedback.message, feedback.c_str());
	send_feedback.len = feedback.size() + 1;

	rc = send_all(cli_sockfd, &send_feedback, sizeof(send_feedback));
	DIE(rc < 0, "send");
	return;
}

bool already_connected(std::string id, int sockfd,
			std::unordered_set<std::string> &used_id_clients,
			std::unordered_set<int> &waiting_id) {

	/* Check if id is already used for a connection. */
	if (used_id_clients.find(id) != used_id_clients.end()) {
		/* Client already exists. */
		std::cout << "Client " + id + " already connected.\n";
		waiting_id.erase(sockfd);
		close(sockfd);
		return true;
	}

	return false;
}

void add_id_client(struct sockaddr_in cli_addr, std::string id,
	std::unordered_map<int, std::string> &id_clients,
	std::unordered_set<std::string> &used_id_clients,
	std::unordered_set<int> &waiting_id, int sockfd) 
{

	id_clients[sockfd] = id;
	used_id_clients.insert(id);
	waiting_id.erase(sockfd);

	std::cout << "New client " + id + " connected from "
		+ std::string(inet_ntoa(cli_addr.sin_addr)) + ":"
		+ std::to_string(ntohs(cli_addr.sin_port)) + ".\n";
}

void run_server(int listenTCP, int listenUDP)
{
	int rc;

	/* Vector containing all sockets. */
	std::vector<struct pollfd> poll_fds;

	/* Corespondence beetween a socket and it's ip and port. */
	std::unordered_map<int, struct sockaddr_in> client_data;

	/* Keep corespondence between sockets and ids. */
	std::unordered_map<int, std::string> id_clients;

	/* Keep online id's from clients. */
	std::unordered_set<std::string> used_id_clients;

	/* Keep sockets that don't have an id. */
	std::unordered_set<int> waiting_id;

	/* Map between ID of a client and the subscribed topics. */
	std::unordered_map<std::string, std::vector<std::vector<std::string>>>
		map_id_topics;

	struct chat_packet received_packet;

	/* Set tcp socket for listening. */
	rc = listen(listenTCP, MAX_CONNECTIONS);
	DIE(rc < 0, "listenTCP");

	struct pollfd poll_tcp = {listenTCP, POLLIN, 0};
	poll_fds.push_back(poll_tcp);

	struct pollfd poll_udp = {listenUDP, POLLIN, 0};
	poll_fds.push_back(poll_udp);

	struct pollfd poll_stdin = {STDIN_FILENO, POLLIN, 0};
	poll_fds.push_back(poll_stdin);

	while (1) {

		rc = poll(&(poll_fds[0]), poll_fds.size(), -1);
		DIE(rc < 0, "poll");

		for (size_t i = 0; i < poll_fds.size(); i++) {
			/* If we have no data coming from
			   current socket, skip processing. */
			if (!(poll_fds[i].revents & POLLIN)) {
				continue;
			}

			/* I have input from this socket. */
			if (poll_fds[i].fd == listenTCP) {
				/* New request connection. */
				new_tcp_connection(listenTCP, poll_fds,
						waiting_id, client_data);
			} else if (poll_fds[i].fd == STDIN_FILENO) {
				if (input_server(poll_fds)) {
					return;
				}
			} else if (poll_fds[i].fd == listenUDP) {
				/* Receive UDP data. */
				struct sockaddr_in cli_addr;
				struct udp_packet udp_client_recv;
				socklen_t socket_len = sizeof(struct sockaddr_in);

				rc = recvfrom(listenUDP, &udp_client_recv,
							sizeof(udp_client_recv), 0,
							(struct sockaddr *)&cli_addr, &socket_len);

				DIE(rc < 0, "recvfrom");

				struct chat_packet send_updates;	
				std::vector<std::string> udp_topic_tokens;
				prepare_data(cli_addr, udp_client_recv,
						 &send_updates, udp_topic_tokens);

				/* Send message to all clients that match the topic. */
				send_subscribers(poll_fds, id_clients, map_id_topics,
						udp_topic_tokens, send_updates);
			} else {
				/* Receive message from client. */
				rc = recv_all(poll_fds[i].fd, &received_packet,
							sizeof(received_packet));
				DIE(rc < 0, "recv");

				if (rc == 0) {
					/* Client disconnected. */
					disconnect_client(poll_fds[i].fd, poll_fds,
							 i, id_clients, used_id_clients);
					continue;
				}

				/* Check if socket is waiting for getting an id. */
				if (waiting_id.find(poll_fds[i].fd) != waiting_id.end()) {
					/* Add client to id_clients. */
					std::string id = std::string(received_packet.message);

					/* Check if id is already connected. */
					if (already_connected(id, poll_fds[i].fd,
							used_id_clients, waiting_id)) {
						/* Remove client from poll. */
						remove_client(i, poll_fds);
						continue;
					}

					/* Now can print connection. */
					add_id_client(client_data[poll_fds[i].fd], id,
						id_clients, used_id_clients, waiting_id,
						poll_fds[i].fd);
				} else {
					process_client(received_packet.message, poll_fds[i].fd,
						id_clients[poll_fds[i].fd], map_id_topics);
				}
			}
		}
	}
}

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	/* Check for running correct command. */
	if (argc != 2) {
		printf("\n Usage: %s <port>\n", argv[0]);
		return 1;
	}

	/* Get the port. */
	uint16_t port;
	int rc = sscanf(argv[1], "%hu", &port);
	DIE(rc != 1, "Given port is invalid");

	/* Create a TCP socket for TCP clients. */
	int listenTCP = socket(AF_INET, SOCK_STREAM, 0);
	DIE(listenTCP < 0, "socketTCP");

	/* Create a UDP socket for UDP clients. */
	int listenUDP = socket(AF_INET, SOCK_DGRAM, 0);
	DIE(listenUDP < 0, "socketUDP");

	/* Complete server's address, address family and port for connection. */
	struct sockaddr_in serv_addr;
	socklen_t socket_len = sizeof(struct sockaddr_in);

	/* Make both sockets reusable and disable nagle algorithm. */
	int enableTCP = 1, enableUDP = 1;
	if (setsockopt(listenTCP, SOL_SOCKET, SO_REUSEADDR, &enableTCP,
			   sizeof(int)) < 0)
		perror("setsockopt(SO_REUSEADDR) failed");
	int flagTCP = 1;
	if (setsockopt(listenTCP, IPPROTO_TCP, TCP_NODELAY, (char *)&flagTCP,
				sizeof(int)) < 0)
		perror("setsockopt(TCP_NODELAY) TCP failed");
	if (setsockopt(listenUDP, SOL_SOCKET, SO_REUSEADDR, &enableUDP,
			   sizeof(int)) < 0)
		perror("setsockopt(SO_REUSEADDR) failed");

	memset(&serv_addr, 0, socket_len);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	rc = inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr.s_addr);
	DIE(rc <= 0, "inet_pton");

	/* Associate the server with listen socket. */
	rc = bind(listenTCP, (const struct sockaddr *)&serv_addr,
		  sizeof(serv_addr));
	DIE(rc < 0, "bind");

	rc = bind(listenUDP, (const struct sockaddr *)&serv_addr,
		  sizeof(serv_addr));
	DIE(rc < 0, "bind");

	/* Run interaction with clients and server. */
	run_server(listenTCP, listenUDP);

	/* Close the TCP and UDP sockets. */
	close(listenTCP);
	close(listenUDP);

	return 0;
}
