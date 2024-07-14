#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <dirent.h>

#include "../src/coapi.h"

#define HTTP "HTTP/1.1"

typedef struct args {
	char *host;
	int port;
	char *root;
} args;

static char root[PATH_MAX];

enum M {
	GET,
	// more to be added
};

static void err_resp(int fd) {
	char *e = HTTP " 405 Method Not Allowed\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"
			"\r\n"
			"<html>\r\n"
			"<head>\r\n"
			"<title>405 Method Not Allowed</title>\r\n"
			"</head>\r\n"
			"<body>\r\n"
			"<h1>Method Not Allowed</h1>\r\n"
			"<p>The method is not allowed.</p>\r\n"
			"</body>\r\n"
			"</html>";
	co_write(fd, e, strlen(e));
}

static void no_content(int fd) {
	char *r = HTTP " 204 No Content\r\n\r\n";
	co_write(fd, r, strlen(r));
}

#define FILE_HEADER HTTP " 200 OK\r\n"\
	"Content-Type: application/octet-stream\r\n"\


#define DIR_HEADER HTTP " 200 OK\r\n"\
		"Content-Type: text/html;charset=utf-8\r\n"\

#define DIR_START "<html>\r\n"\
		"<head>\r\n"\
		"<title></title>\r\n"\
		"</head>\r\n"\
		"<body>\r\n"

#define DIR_END \
		"</body>\r\n"\
		"</html>"


static char *filename(char *file) {
	char *s = file;
	while (*file) {
		if (*file++ == '/')s=file;
	}
	return s;
}

static void return_file(char *file, size_t size, int fd) {
	FILE *f = fopen(file, "rb");
	if (!f) {
		err_resp(fd);
		return;
	}
	char buf[4096];
	int n;
	strcpy(buf, FILE_HEADER);
	strcat(buf, "Content-Disposition: attachment; filename=\"");
	strcat(buf, filename(file));
	strcat(buf, "\"\r\n");
	strcat(buf, "Content-Length: ");
	char temp[64];
	snprintf(temp, sizeof(temp), "%d", size);
	strcat(buf, temp);
	strcat(buf, "\r\n\r\n"); // end of header

	co_write(fd, buf, strlen(buf));
	int count = 0;
	while ((n = co_fread(buf, 1, sizeof(buf), f)) > 0) {
		co_write(fd, buf, n);
		count+= n;
	}
#ifdef DEBUG
	co_printf("file size: %d, download size: %d\n", size, count);
#endif

	fclose(f);
}

static void return_dir(char *dir, char *rp, int fd) {
	DIR *d = opendir(dir);
	if (!d) {
		err_resp(fd);
		return;
	}
#define SIZ (10 * 4096)
	char *buf = malloc(SIZ);
	char temp[64];

	strcpy(buf, DIR_START);
	strcat(buf, "<h1>");
	strcat(buf, rp);
	strcat(buf, "</h1>\r\n");
	strcat(buf, "<ol>\n");

	int len = strlen(buf);
	struct dirent *e;
	char b[1024];
	char * ctx = filename(rp);
	while (e = co_readdir(d)) {
		strcpy(b, "<li>\n");
		strcat(b, "<a href=\"");
		if (*ctx) {
			strcat(b, ctx);
			strcat(b, "/");
		}
		strcat(b, e->d_name);
		strcat(b, "\">");
		strcat(b, e->d_name);
		strcat(b, "</a>");
		if (e->d_type == DT_REG) {
			struct stat st;
			char p[PATH_MAX];
			strcpy(p, dir);
			strcat(p, "/");
			strcat(p, e->d_name);
			if (stat(p, &st)) {
				co_printf("error stat file: %s", e->d_name);
				continue;
			}
			snprintf(temp, sizeof(temp), "    %d", st.st_size);
			strcat(b, temp);
		}
		strcat(b, "</li>\n");
		int n = strlen(b);
		if (len + n >= SIZ) {
			// too many
			break;
		}
		strcat(buf, b);
		len += n;

	}
	strcpy(b, "</ol>\n");
	strcat(b, DIR_END);
	strcat(buf, b);
	len += strlen(b);

	snprintf(temp, sizeof(temp), "%d\r\n", len);
	strcpy(b, DIR_HEADER);
	strcat(b, "Content-Length: ");
	strcat(b, temp);
	strcat(b, "\r\n");
	co_write(fd, b, strlen(b));

	co_write(fd, buf, len);

	free(buf);
	closedir(d);
}

static inline int from_hex(char *s) {
	if (*s >= 'a' && *s <= 'f') {
		return 10 + (*s - 'a');
	} else if (*s >= 'A' && *s <= 'F') {
		return 10 + (*s - 'A');
	} else if (*s >= '0' && *s <= '9') {
		return *s - '0';
	}
	return 0;
}

static char *url_decode(char *p, char *dst, int max) {
	char *s = p;
	while (*s) {
		if (*s == '%') {
			*dst++ = (char)(from_hex(s+1) << 4 | from_hex(s+2));
			s+=3;
		} else {
			*dst++=*s++;
		}
	}
	*dst = '\0';
	return dst;
}

static void handle_request(char *s, int len, int fd) {
	int n = 0;
	char *p = s;
	while (*p++ != ' ') n++; // method
	if (strncmp("GET", s, n)) {
		err_resp(fd);
		return;
	}
	s = p;
	n = 0;
	while (*p++ != ' ') n++; // path
	s[n] = '\0';
	char path[PATH_MAX];
	strcpy(path, root);
	char dec[PATH_MAX];
	url_decode(s, dec, PATH_MAX);
	if (!strcmp("/favicon.ico", dec)) {
		no_content(fd);
		return;
	}
	strncat(path, dec, n);
	struct stat st;
	if (stat(path, &st)) {
		err_resp(fd);
		return;
	}
	if (S_ISREG(st.st_mode)) {
		return_file(path, st.st_size, fd);
	} else if (S_ISDIR(st.st_mode)) {
		return_dir(path, dec, fd);
	}
}

void *handle_connnect(void *a) {
	int sfd  = (int)a;
#define BUFSZ 1024
	char buf[BUFSZ];
	while (1) {
		int n = co_read(sfd, buf, BUFSZ-1);
		if (n <= 0) {
			goto out;
		}
		buf[n] = '\0';
		handle_request(buf, n, sfd);
	}
out:
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
	co_printf("web server started at %#x:%d\n", ntohl(*(int*)&addr.sin_addr), a->port);

	if (a->root) {
		strncpy(root, a->root, sizeof(root));
	} else {
		if (!getcwd(root, sizeof(root))) {
			err_exit("unable to get root");
		}
	}

	struct sockaddr_in sa;
	socklen_t sl = sizeof(sa);
	int fd;

	while (1) {
		fd = co_accept(so, &sa, &sl);
		if (fd == -1) {
			perror("error accepting request");
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
	args a = {"127.0.0.1", 8080, NULL};
	// poor man's arg parse
	for (int i = 1; i< argc; ++i) {
		if (!strcmp(argv[i], "-h") && i < argc - 1) {
			a.host = argv[i+1];
			i+=1;
		} else if (!strcmp(argv[i], "-p") && i < argc - 1) {
			a.port = atoi(argv[i+1]);
			i+=1;
		} else if (!strcmp(argv[i], "-r") && i < argc - 1) {
			a.root = argv[i+1];
			i+=1;
		}
	}
	coro_start(start_server, &a);
}

