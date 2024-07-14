#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "coro.h"

void __sched_timer(ctimer *ct);

void __sched_epoll(int fd, struct epoll_event *ev);

void co_sleep(struct timespec *ts) {
	if (ts->tv_sec <= 0 && ts->tv_nsec <= 0) {
		return;
	}
	__sched_timer(ts);
}

int co_socket(int domain, int type, int proto) {
	int so = socket(domain, type, proto);
	int flags = fcntl(so, F_GETFL);
	err_guard(fcntl(so, F_SETFL, flags | O_NONBLOCK), "error creating socket");
	return so;
}

int co_accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len) {
	int n;
	while ((n = accept(fd, addr, len)) == -1
			&& (errno == EAGAIN || errno == EWOULDBLOCK)) {
		struct epoll_event ev = { 0 };
		epoll_t data;
		data.task = current;
		data.fd = fd;
		ev.data.ptr = &data;
		ev.events = EPOLLIN;
		__sched_epoll(fd, &ev);
	}
	if (n == -1) {
		return n;
	}
	int __flags = fcntl(n, F_GETFL);
	if (fcntl(n, F_SETFL, __flags | O_NONBLOCK) == -1) {
		return -1;
	}
	return n;
}

ssize_t co_read(int sfd, void* buf, size_t size) {
	int n;
	while ((n = read(sfd, buf, size)) == -1
			&& (errno == EAGAIN || errno == EWOULDBLOCK)) {
		struct epoll_event ev = { 0 };
		epoll_t data;
		data.task = current;
		data.fd = sfd;
		ev.data.ptr = &data;
		ev.events = EPOLLIN;
		__sched_epoll(sfd, &ev);
	}
	return n;
}

ssize_t co_write(int fd, const void *buf, size_t size) {
	int n;
	while ((n = write(fd, buf, size)) == -1
			&& (errno == EAGAIN || errno == EWOULDBLOCK)) {
		struct epoll_event ev = { 0 };
		epoll_t data;
		data.task = current;
		data.fd = fd;
		ev.data.ptr = &data;
		ev.events = EPOLLOUT;
		__sched_epoll(fd, &ev);
	}
	err_guard(n, "error write");
	return n;
}
