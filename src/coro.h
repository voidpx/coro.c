#ifndef SRC_CORO_H_
#define SRC_CORO_H_

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include "list.h"

typedef enum state {
	NEW,
	RUNNABLE,
	BLOCKED,
	DYING,
	DEAD
} state;

typedef struct task {
	unsigned long sp;
	unsigned long pc;
	void *arg;
	void *ret;
	unsigned long stack;
	list link;
	int preempt;
	state state;
	nlist_head waitq; // who is waiting for this to finish
	char name[64];
	int flags;
} task;

typedef struct ctimer {
	struct timespec expire;
	task *task;
} ctimer;

typedef struct epoll_t {
	task *task;
	int fd;
} epoll_t;

/*
 * call into the scheduler
 */
void schedule();

/*
 * create a coroutine that will execute func with the argument arg, can only be called from within a coroutine,
 * i.e. not outside the runtime.
 */
task *coro(void *(*func)(void *), void *arg, char* name, int flags);

/*
 * main entry point for the coroutine runtime, it sets up the runtime and execute the main function
 * with the given argument arg
 */
void *coro_start(void *(*main)(void*), void *arg);

/*
 * wait for the given task to finish and return the result, can only be called from a coroutine.
 */
void *wait_for(task *t);

#define TF_REAPER 0x1
#define TF_DETACHED 0x2
#define is_reaper(t) ((t->flags & TF_REAPER) == TF_REAPER)
#define is_dead(t) (t->state == DEAD)
#define is_dying(t) (t->state == DYING)
#define is_detached(t) ((t->flags & TF_DETACHED) == TF_DETACHED)

#define err_exit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                               } while (0)

#define err_guard(call, msg) ({int __ret = (call);if (__ret == -1) err_exit(msg); __ret;})

#define UNSAFE 0x1
#define RESCHED 0x2

extern task *current;
extern nlist_head ctimers;
extern int epollfd;

#define preempt_disable() current->preempt |= UNSAFE
#define preempt_enable() current->preempt &= ~UNSAFE
#define preempt_disabled() (current->preempt & UNSAFE)
#define async_preempt() current->preempt |= RESCHED
#define should_resched() (current->preempt & RESCHED)
#define clear_resched() current->preempt &= ~RESCHED

#define runnable(t) (t->state == RUNNABLE || t->state == NEW)

#define for_each_task(t, link, head) \
	list_for_each(t, link, head)

#define printf(fmt, ...) \
  ({ \
	preempt_disable();\
	int __ret; \
	__ret = printf(fmt, ##__VA_ARGS__); \
	preempt_enable();\
	if (should_resched()) { clear_resched(); schedule();}\
	__ret;})

void co_sleep(int nsec);
int co_socket(int domain, int type, int proto);
int co_accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len);
ssize_t co_read(int fd, void* buf, size_t n);
ssize_t co_write(int fd, const void *buf, size_t n);

#endif /* SRC_CORO_H_ */
