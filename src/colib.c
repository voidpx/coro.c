#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "coro.h"

void co_sleep(int nsec) {
	if (nsec <= 0) {
		return;
	}
	nlist_node __n = { NULL, NULL, NULL };
	ctimer ct;
	err_guard(clock_gettime(CLOCK_MONOTONIC, &ct.expire), "handler_timer");
	ct.expire.tv_sec += nsec;
	ct.task = current;
	__n.n = &ct;
	preempt_disable();
	nlist_push(&__n, &ctimers);
	current->state = BLOCKED;
	preempt_enable();
	schedule();
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
		preempt_disable();
		struct epoll_event ev = { 0 };
		epoll_t data;
		data.task = current;
		data.fd = fd;
		ev.data.ptr = &data;
		ev.events = EPOLLIN;
		err_guard(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev),
				"error epoll_ctl");
		current->state = BLOCKED;
		preempt_enable();
		schedule();
	}
	err_guard(n, "error accept");
	int __flags = fcntl(n, F_GETFL);
	err_guard(fcntl(n, F_SETFL, __flags | O_NONBLOCK),
			"error setting non-blocking");
	return n;
}

ssize_t co_read(int sfd, void* buf, size_t size) {
	int n;
	while ((n = read(sfd, buf, size)) == -1
			&& (errno == EAGAIN || errno == EWOULDBLOCK)) {
		preempt_disable();
		struct epoll_event ev = { 0 };
		epoll_t data;
		data.task = current;
		data.fd = sfd;
		ev.data.ptr = &data;
		ev.events = EPOLLIN;
		err_guard(epoll_ctl(epollfd, EPOLL_CTL_ADD, sfd, &ev),
				"error epoll_ctl");
		current->state = BLOCKED;
		preempt_enable();
		schedule();
	}
	err_guard(n, "error read");
	return n;
}

ssize_t co_write(int fd, const void *buf, size_t size) {
	int n;
	while ((n = write(fd, buf, size)) == -1
			&& (errno == EAGAIN || errno == EWOULDBLOCK)) {
		preempt_disable();
		struct epoll_event ev = { 0 };
		epoll_t data;
		data.task = current;
		data.fd = fd;
		ev.data.ptr = &data;
		ev.events = EPOLLOUT;
		err_guard(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev),
				"error epoll_ctl");
		current->state = BLOCKED;
		preempt_enable();
		schedule();
	}
	err_guard(n, "error write");
	return n;
}
