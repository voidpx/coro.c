#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "../src/coro.h"

void *handle_connnect(void *a) {
	int sfd  = (int)a;
#define BUFSZ 1024
	char buf[BUFSZ];
	while (1) {
		int n = err_guard(co_read(sfd, buf, BUFSZ-1), "error read");
		err_guard(co_write(sfd, buf, n), "error write");
		if (n > 0) {
			buf[n] = '\0';
			printf("connection %d recv: %s", sfd, buf);
		}
	}
}

void *start_server(void *arg) {
	int so = co_socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr = (struct in_addr){htonl(INADDR_ANY)};
	addr.sin_port = htons(8888);
	err_guard(bind(so, (struct sockaddr*)&addr, sizeof(addr)), "error bind");
	listen(so, 50);
	struct sockaddr sa;
	socklen_t sl;
	int fd;
	while (1) {
		fd = err_guard(co_accept(so, &sa, &sl), "error accept");
#define N 32
		char s[N];
		snprintf(s, N, "connection accepted: %d\n", fd);
		printf("%s", s);
		coro(handle_connnect, (void*) fd, s);

	}
	return NULL;
}

int main() {
	coro_start(start_server, NULL);
}



