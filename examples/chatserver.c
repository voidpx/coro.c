#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "../src/coro.h"

#define UNREG 0x7f
#define HTAB_MAX 1<<10
#define MAX_USERS 1<<10

typedef struct args {
	char *host;
	int port;
} args;

typedef struct hnod {
	struct hnod *next;
	void *key;
	void *data;
} hnod;

typedef struct htab {
	hnod **tab;
	size_t len;
	size_t (*hash)(void *);
	int (*equal)(void *, void*);
} htab;

htab user_registry;

char **conns;

// http://www.cse.yorku.ca/~oz/hash.html
size_t str_hash(char *str) {
	size_t hash = 5381;
	int c;
	while (c = *str++)
		hash = ((hash << 5) + hash) + c;
	return hash;
}

int str_equal(char *s1, char *s2) {
	return !strcmp(s1, s2);
}

void init_htab() {
	user_registry.tab = co_calloc(HTAB_MAX, sizeof(hnod*));
	if (!user_registry.tab) {
		err_exit("error allocating registry");
	}
	user_registry.hash = str_hash;
	user_registry.equal = str_equal;
	user_registry.len = 0;

	conns = co_calloc(HTAB_MAX, sizeof(*conns));
}

void *htab_get(htab *h, void *k) {
	int i = h->hash(k) % HTAB_MAX;
	hnod *n = h->tab[i];
	while (n && !h->equal(n->key, k)) {
		n = n->next;
	}
	return n ? n->data : NULL;
}

void htab_remove(htab *h, void* k) {
	int i = h->hash(k) % HTAB_MAX;
	hnod *n = h->tab[i];
	hnod *prev = NULL;
	while (n && !h->equal(n->key, k)) {
		prev = n;
		n = n->next;
	}
	if (n) {
		if (prev) {
			prev->next = n->next;
		} else {
			h->tab[i] = NULL;
		}
		co_free(n->key);
		co_free(n);
	}
}

void htab_put(htab *h, void *k, void *data) {
	hnod *new = co_calloc(1, sizeof(*new));
	if (!new) {
		err_exit("error htab_put");
	}
	new->key = k;
	new->data = data;
	int i = h->hash(k) % HTAB_MAX;
	hnod *n = h->tab[i];
	if (!n) {
		h->tab[i] = new;
	} else {
		while (n->next) {
			n=n->next;
		}
		n->next = new;
	}
}

static char *dup_str(char *s, int len) {
	char *dup = co_malloc(len + 1);
	strncpy(dup, s, len);
	dup[len] = '\0';
	return dup;
}

static char* copy_user(char *s, int len, char delimiter) {
	char *t = s;
	while (len && *t != delimiter && *t != '\n') t++, len--;
	int n = t - s;
	return dup_str(s, n);
}

char *_after(char *s, char c, size_t len) {
	char *t = s + len;
	while (s < t && *s != c) s++;
	if (s >= t - 1) {
		return NULL;
	}
	return s+1;
}


int write_fmt_va(int so, char *fmt, va_list va) {
	char buf[256];
	int n = vsnprintf(buf, sizeof(buf), fmt, va);
	if (n > sizeof(buf)) {
		// too many
		n = sizeof(buf);
	}
	return err_guard(co_write(so, buf, n), "error write");
}

int write_fmt(int so, char *fmt, ...) {
	va_list l;
	va_start(l, fmt);
	int n = write_fmt_va(so, fmt, l);
	va_end(l);
	return n;
}

int write_fmt_color(int so, int fd, char *fmt, ...) {
	int r, g, b;
	int h = fd*0x9E3779B1;
	if (fd == 0) { // server broad cast
		r = 0; g = 0xff; b = 0xff;
	} else {
		r = (0x5a ^ h) & 0xff; g = (0xa5 ^ (h>>8)) & 0xff; b = (0x55 ^ (h>>16)) & 0xff;
	}
	va_list l;
	va_start(l, fmt);
	char buf[512];
	int n = vsnprintf(buf, sizeof(buf), fmt, l);
	if (n > sizeof(buf)) {
		// too many
		n = sizeof(buf);
	}
	va_end(l);
	buf[sizeof(buf) - 1] = '\0';
	char *template = "\x1b[38;2;%d;%d;%dm%s\x1b[m";
	return write_fmt(so, template, r, g, b, buf);
//	[38;2;240;100;200m\e[48;2;200;255;50m
}

char *skip_space(char *s, int len) {
	char *p = s+len;
	while (s < p && *s == ' ') s++;
	return s;
}

char *next_space(char *s, int len) {
	char *p = s+len;
	while (s < p && !isspace(*s)) s++;
	return s;
}


static void list_users(int fd) {
	int first = 1;
	for (int i = 0; i < MAX_USERS; ++i) {
		if (conns[i] && conns[i] != (char*)UNREG) {
			if (first) {
				first = 0;
				write_fmt_color(fd, 0, "%s", conns[i]);
			} else {
				write_fmt_color(fd, 0, ", %s", conns[i]);
			}
		}
	}
	write_fmt_color(fd, 0, "\n");
}

#define broadcast(fd, fmt, ...)\
	do {\
		for (int i = 0; i < MAX_USERS; ++i) {\
			if (conns[i]) {\
				write_fmt_color(i, fd, fmt, __VA_ARGS__);\
			}\
		}} while(0)

void handle_register(char *s, int len, int fd, int unreg) {
	if (!unreg) {
		char *e = next_space(s, len);
		int n = e - s;
		if (!n) {
			write_fmt_color(fd, 0, "user name required\n");
			return;
		}
		len -= n;
		char *user = dup_str(s,n);
		if (htab_get(&user_registry, user)) {
			write_fmt_color(fd, 0, "unable to register, user exists: %s\n", user);
		} else {
			htab_put(&user_registry, user, (void*)fd);
			conns[fd] = user;
			broadcast(0, "user registered: %s\n", user);
		}
	} else {
		char *user = conns[fd];
		if (user && user != (char*)UNREG) {
			broadcast(0, "%s unregistered\n", user);
			htab_remove(&user_registry, user);
			conns[fd] = (char*)UNREG;
		} else {
			write_fmt_color(fd, 0, "you are not registered\n");
		}
	}
}

void handle_cmd(char *s, int len, int fd) {
	char *e = next_space(s, len);
	int n = e-s;
	char *sp = skip_space(e, len - n);
	if (n == 1 && !strncmp(s, "r", n)) {
		handle_register(sp, len - (sp - s), fd, 0);
	} else if (n == 2 && !strncmp(s, "r!", n)) {
		handle_register(sp, len - (sp - s), fd, 1);
	} else if (n == 1 && !strncmp(s, "l", n)) {
		list_users(fd);
	}
}

void handle_msg(char *s, int len, int fd) {
	if (*s == ':') {// cmd
		handle_cmd(s+1, len - 1, fd);
	} else if (*s == '@') { // private message
		char *from = conns[fd];
		if (!from || from == (char *)UNREG) {
			write_fmt_color(fd, 0, "please register(:r) first to send private messages\n");
		} else {
			char *to = copy_user(s+1, len -1, ':');
			char *m = _after(s+1, ':', len - 1);
			void *d = htab_get(&user_registry, to);
			if (!d) {
				write_fmt_color(fd, 0, "user doesn't exist: %s\n", to);
			} else {
				if (m) {
					int to = (int) d;
					write_fmt_color(to, fd, "%s:%s", from, m);
				} else {
					write_fmt_color(fd, 0, "no message specified\n");
				}
			}
		}
	} else {
		broadcast(fd, "%s", s);
	}
}

#define HEAD "welcome!\n"\
	"commands:\n"\
	":r <user> - register\n"\
	":r! - unregister\n"\
	":l - list all online users\n"\
	"@<user>:<message> - send private message to <user>\n"

void *handle_connnect(void *a) {
	int sfd  = (int)a;
	conns[sfd] = (char *)UNREG; // unregistered
	write_fmt_color(sfd, 0, HEAD);
	write_fmt_color(sfd, 0, "currently online: ");
	list_users(sfd);
#define BUFSZ 1024
	char buf[BUFSZ];
	while (1) {
		int n = co_read(sfd, buf, BUFSZ-1);
		if (n <= 0) {
			break;
		}
		buf[n] = '\0';
		handle_msg(buf, n, sfd);
	}
	close(sfd);
	return NULL;
}

void *start_server(void *arg) {
	args *a = (args*)arg;
	struct addrinfo ah = {0};
	ah.ai_family = AF_INET;
	ah.ai_socktype = SOCK_STREAM;
	ah.ai_protocol = 0;
	struct addrinfo *ai;
	if (getaddrinfo(a->host, NULL, &ah, &ai)) {
		err_exit("unable to resolve host");
	}
	int so = co_socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = *(struct sockaddr_in*)ai->ai_addr;
	freeaddrinfo(ai);
	addr.sin_port = htons(a->port);
	err_guard(bind(so, &addr, sizeof(addr)), "error bind");
	listen(so, 50);
	co_printf("chat server started at %#x:%d\n", ntohl(*(int*)&addr.sin_addr), a->port);
	struct sockaddr_in sa;
	socklen_t sl = sizeof(sa);
	int fd;
	init_htab();
	while (1) {
		fd = err_guard(co_accept(so, &sa, &sl), "error accept");
		if (fd >= MAX_USERS) {
			write_fmt_color(fd, 0, "connection limit exceeded\n");
			close(fd);
			continue;
		}
#define N 64
		char s[N];
		co_snprintf(s, N, "connection: %d, addr: %#x, port: %d\n", fd,
				ntohl(*(int*)&sa.sin_addr), ntohs(sa.sin_port));
		co_printf("%s", s);
		coro(handle_connnect, (void*) fd, s, TF_DETACHED);

	}
	return NULL;
}

int main(int argc, char **argv) {
	args a = {"127.0.0.1", 5555};
	if (argc >= 3) {
		a.host = argv[1];
		a.port = atoi(argv[2]);
	} else if (argc >= 2) {
		a.port = atoi(argv[1]);
	}
	coro_start(start_server, &a);
}

