#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const int LISTEN_PORT = 9999;
static const int DEFAULT_BUFFER_SIZE = 1024;
static const int DEFAULT_BACKLOG = 1024;

static int stop_flag = 0;

static void sigterm_handler(int sig)
{
	char *msg = "Receive SIGTERM signal, stop server.\n";
	write(STDOUT_FILENO, msg, strlen(msg));
	stop_flag = 1;
}

int main(int argc, char *argv[])
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		perror("Fail to create socket");
		exit(-1);
	}

	struct sockaddr_in bind_addr;
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(LISTEN_PORT);
	if (inet_aton("0.0.0.0", &bind_addr.sin_addr) == 0) {
		perror("Fail to parse net address");
		exit(-1);
	}

	if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == -1) {
		perror("Fail to bind");
		exit(-1);
	}

	if (listen(fd, DEFAULT_BACKLOG) == -1) {
		perror("Fail to listen");
		exit(-1);
	}

	struct sockaddr_in peer_addr;
	socklen_t addr_len = sizeof(peer_addr);

	struct sigaction term_action;
	term_action.sa_handler = sigterm_handler;
	term_action.sa_flags = SA_NODEFER;
	if (sigaction(SIGTERM, &term_action, NULL) == -1) {
		perror("Fail to catch SIGTERM signal.");
	}

	char *msg = "Listening...\n\n";
	write(STDOUT_FILENO, msg, strlen(msg));
	while (!stop_flag) {
		int cfd = accept(fd, (struct sockaddr *)&peer_addr, &addr_len);
		if (cfd == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("Fail to accept client request");
			exit(-1);
		}

		char buffer[DEFAULT_BUFFER_SIZE];
		int size;
		while ((size = read(cfd, buffer, DEFAULT_BUFFER_SIZE)) != 0) {
			if (size == -1) {
				if (errno == EINTR) {
					continue;
				}

				perror("Fail to recv from client");
				goto END_REQUEST;
			}

			write(STDOUT_FILENO, buffer, size);
			if (size >= 4 &&
			    memcmp("\r\n\r\n", buffer + size - 4, 4) == 0) {
				break;
			}
		}

		size = snprintf(buffer, DEFAULT_BUFFER_SIZE,
				"HTTP/1.1 200 OK\r\n"
				"Content-Length: 21\r\n\r\n"
				"<h1>Hello world!</h1>");
		int wsize = write(cfd, buffer, size);
		if (wsize == -1) {
			perror("Fail to send to client.");
			goto END_REQUEST;
		}

		if (size != wsize) {
			fprintf(stderr, "Fail to send all data to client");
		}

	END_REQUEST:;
		if (close(cfd) == -1) {
			perror("Fail to close client socket");
		}
	}

	msg = "Stop listening!\n";
	write(STDOUT_FILENO, msg, strlen(msg));

	if (close(fd) == -1) {
		perror("Fail to close socket");
		exit(-1);
	}

	return 0;
}
