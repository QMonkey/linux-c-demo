/*
 * multi process version blocking server
 */
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
#include <sys/wait.h>
#include <unistd.h>

#define LISTENING_PORT 9999

static const int DEFAULT_BUFFER_SIZE = 1024;
static const int DEFAULT_BACKLOG = 1024;

static int stop_flag = 0;

static void sigterm_handler(int sig)
{
	char *msg = "Receive SIGTERM signal, stop server.\n";
	write(STDOUT_FILENO, msg, strlen(msg));
	stop_flag = 1;
}

static void sigkill_handler(int sig)
{
	char *msg = "Just for fun, I will still die. :)\n";
	write(STDOUT_FILENO, msg, strlen(msg));
	stop_flag = 1;
}

static void sigchld_handler(int sig)
{
	int status = 0;
	int res = 0;
	do {
		res = waitpid(-1, &status, WNOHANG);
		if (res == -1) {
			perror("Fail to wait child process");
			exit(-1);
		}
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));
}

/*
 * handle request by simply echo what client send
 * @param 	client_fd
 * @return 	int
*/
int handle_request(int client_fd)
{
	char buffer[DEFAULT_BUFFER_SIZE];
	int size;
	int res;

	while ((size = read(client_fd, buffer, DEFAULT_BUFFER_SIZE)) != 0) {
		if (size == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("Fail to recv from client");
			return errno;
		}

		write(STDOUT_FILENO, buffer, size);
		if (size >= 4 &&
		    memcmp("\r\n\r\n", buffer + size - 4, 4) == 0) {
			break;
		}
	}

	size =
	    snprintf(buffer, DEFAULT_BUFFER_SIZE, "HTTP/1.1 200 OK\r\n"
						  "Content-Length: 21\r\n\r\n"
						  "<h1>Hello world!</h1>");

	int wsize = write(client_fd, buffer, size);
	if (wsize == -1) {
		perror("Fail to send to client.");
		return errno;
	}

	if (size != wsize) {
		fprintf(stderr, "Fail to send all data to client");
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int res;
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		perror("Fail to create socket");
		exit(-1);
	}

	struct sockaddr_in bind_addr;
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(LISTENING_PORT);
	res = inet_aton("0.0.0.0", &bind_addr.sin_addr);
	if (res == 0) {
		perror("Fail to parse net address");
		exit(-1);
	}

	/* make port reusable */
	int on = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0) {
		perror("setsockopt");
		exit(-1);
	}

	res = bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (res == -1) {
		perror("Fail to bind");
		exit(-1);
	}

	res = listen(fd, DEFAULT_BACKLOG);
	if (res == -1) {
		perror("Fail to listen");
		exit(-1);
	}

	struct sockaddr_in peer_addr;
	socklen_t addr_len = sizeof(peer_addr);

	// register SIGTERM handler
	struct sigaction term_action;
	term_action.sa_handler = sigterm_handler;
	term_action.sa_flags = SA_NODEFER;
	res = sigaction(SIGTERM, &term_action, NULL);
	if (res == -1) {
		perror("Fail to catch SIGTERM signal.");
	}

	// register SIGKILL handler
	struct sigaction kill_action;
	kill_action.sa_handler = sigkill_handler;
	kill_action.sa_flags = SA_NODEFER;
	res = sigaction(SIGKILL, &kill_action, NULL);
	if (res == -1) {
		perror("Fail to catch SIGKILL signal.");
	}

	// register SIGCHLD handler
	struct sigaction chld_action;
	chld_action.sa_handler = sigchld_handler;
	chld_action.sa_flags = SA_NODEFER;
	res = sigaction(SIGCHLD, &chld_action, NULL);
	if (res == -1) {
		perror("Fail to catch SIGCHLD signal.");
	}

	char *msg = "Listening...\n\n";
	write(STDOUT_FILENO, msg, strlen(msg));
	pid_t pid = 0;
	while (!stop_flag) {
		int cfd = accept(fd, (struct sockaddr *)&peer_addr, &addr_len);
		if (cfd == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("Fail to accept client request.");
			exit(-1);
		}

		if ((pid = fork()) != -1) {
			perror("Fail to fork");
			exit(-1);
		} else if (pid == 0) {
			int handle_res = 0;
			handle_res = handle_request(cfd);

			res = close(cfd);
			if (res == -1) {
				perror("Fail to close client socket.");
				return res;
			}

			return handle_res;
		}
	}

	msg = "Stop listening!\n";
	write(STDOUT_FILENO, msg, strlen(msg));

	res = close(fd);
	if (res == -1) {
		perror("Fail to close socket");
		exit(-1);
	}

	return 0;
}
