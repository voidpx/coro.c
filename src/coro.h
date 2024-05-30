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

// from musl
typedef struct co_fpstate {
	unsigned short cwd, swd, ftw, fop;
	unsigned long long rip, rdp;
	unsigned mxcsr, mxcr_mask;
	struct {
		unsigned short significand[4], exponent, padding[3];
	} _st[8];
	struct {
		unsigned element[4];
	} _xmm[16];
	unsigned padding[24];
} co_fpstate;
typedef struct co_context {
	unsigned long r8, r9, r10, r11, r12, r13, r14, r15;
	unsigned long rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, eflags;
	unsigned short cs, gs, fs, __pad0;
	unsigned long err, trapno, oldmask, cr2;
} co_context;

typedef struct task_context {
	co_context context;
	co_fpstate fpstate;
} task_context;

typedef struct task {
	unsigned long ip;
	unsigned long sp;
	void *arg;
	void *ret;
	unsigned long stack;
	list link;
	int preempt;
	state state;
	nlist_head waitq; // who is waiting for this to finish
	char name[64];
	int flags;
	int reserved;  // align
	task_context ctx;
	unsigned long stack_last_mapped;
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
#define err_out(fmt,...) do{fprintf(stderr, fmt, __VA_ARGS__);}while(0)

#define err_guard(call, msg) ({int __ret = (call);if (__ret == -1) err_exit(msg); __ret;})

#define UNSAFE 0x1
#define RESCHED 0x2

extern task *current;
extern int __preempt;

#define preempt_disable() __preempt |= UNSAFE
#define preempt_enable() __preempt &= ~UNSAFE
#define preempt_disabled() (__preempt & UNSAFE)
#define async_preempt() __preempt |= RESCHED
#define should_resched() (__preempt & RESCHED)
#define clear_resched() __preempt &= ~RESCHED

#define runnable(t) (t->state == RUNNABLE || t->state == NEW)

#define for_each_task(t, link, head) \
	list_for_each(t, link, head)

#define call_no_preempt(func, rtype, ...) \
		({ \
			preempt_disable();\
			rtype __ret; \
			__ret = func(__VA_ARGS__); \
			preempt_enable();\
			if (should_resched()) { clear_resched(); schedule();}\
			__ret;})

#define call_no_preempt_void(func, ...) \
		({ \
			preempt_disable();\
			func(__VA_ARGS__); \
			preempt_enable();\
			if (should_resched()) { clear_resched(); schedule();}\
			0;})

#define co_printf(fmt, ...) \
  ({ \
	call_no_preempt(printf, int, fmt, ##__VA_ARGS__);})

#define co_snprintf(s, size, fmt, ...) \
		({ \
			call_no_preempt(snprintf, int, s, size, fmt, ##__VA_ARGS__);})

#define co_malloc(size) \
	({\
		   call_no_preempt(malloc, void *, size);\
	})

#define co_calloc(count, elesize) \
	({\
		   call_no_preempt(calloc, void *, count, elesize);\
	})

#define co_free(p) \
	({\
		   call_no_preempt_void(free, p);\
	})

#define co_clock_gettime(clock, ts)\
		({\
				   call_no_preempt(clock_gettime, int, clock, ts);\
			})

#define co_localtime(time)\
		({\
				   call_no_preempt(localtime, struct tm *, time);\
			})

#define co_strftime(s, size, fmt, tm)\
		({\
				   call_no_preempt(strftime, size_t, s, size, fmt, tm);\
			})

void co_sleep(struct timespec *ts);
int co_socket(int domain, int type, int proto);
int co_accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len);
ssize_t co_read(int fd, void* buf, size_t n);
ssize_t co_write(int fd, const void *buf, size_t n);

#endif /* SRC_CORO_H_ */
