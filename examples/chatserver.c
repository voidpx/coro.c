#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdlib.h>

#include "../src/coro.h"

#define PORT 9527
#define HTAB_MAX 1000

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
	user_registry.tab = calloc(HTAB_MAX, sizeof(hnod*));
	if (!user_registry.tab) {
		err_exit("error allocating registry");
	}
	user_registry.hash = str_hash;
	user_registry.equal = str_equal;
	user_registry.len = 0;
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
		free(n->key);
		free(n);
	}
}

void htab_put(htab *h, void *k, void *data) {
	hnod *new = calloc(1, sizeof(*new));
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

static char* copy_user(char *s, char delimiter) {
	char *t = s;
	while (*t != delimiter) t++;
	int n = t - s;
	if (n > 30) n = 30;
	char *user = malloc(n + 1);
	strncpy(user, s, n);
	user[n] = '\0';
	return user;
}

char *find_char(char *s, char c, size_t len) {
	char *t = s;
	while (*t++ != c && (t-s) < len);
	if (t == s + len) {
		return NULL;
	}
	return t;
}

void handle_msg(char *s, int len, int fd) {
	if (*s == '&') {
		char c = *(s+1);
		if (c == '=') { // register
			char *user = copy_user(s+2, '\n');
			htab_put(&user_registry, user, (void*)fd);
			char *ur = "user registered:";
			err_guard(co_write(fd, ur, strlen(ur)), "error write");
			err_guard(co_write(fd, user, strlen(user)), "error write");
		} else if (c == '!') {
			char *user = copy_user(s+2, '\n');
			htab_remove(&user_registry, user);
			free(user);
		}
	} else if (*s == '@') {
		char *to = copy_user(s+1, ':');
		char *m = find_char(s+1, ':', len - 1);
		if (m) {
			void *d = htab_get(&user_registry, to);
			if (!d) {
				char *no_user = "user doesn't exist: ";
				err_guard(co_write(fd, no_user, strlen(no_user)), "error write");
				err_guard(co_write(fd, to, strlen(to)), "error write");
			} else {
				int fd = (int)d;
				err_guard(co_write(fd, m, s+len - m), "error write");
			}
		}
	}

}

void *handle_connnect(void *a) {
	int sfd  = (int)a;
#define BUFSZ 1024
	char buf[BUFSZ];
	while (1) {
		int n = err_guard(co_read(sfd, buf, BUFSZ-1), "error read");
		//err_guard(co_write(sfd, buf, n), "error write");
		if (n > 0) {
			handle_msg(buf, n, sfd);
//			buf[n] = '\0';
//			printf("connection %d recv: %s", sfd, buf);
		}
	}
}

void *start_server(void *arg) {
	int so = co_socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr = (struct in_addr){htonl(INADDR_ANY)};
	addr.sin_port = htons(PORT);
	err_guard(bind(so, (struct sockaddr*)&addr, sizeof(addr)), "error bind");
	listen(so, 50);
	struct sockaddr sa;
	socklen_t sl;
	int fd;
	printf("chat server started\n=======================\n");
	init_htab();
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

