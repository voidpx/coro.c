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
	RUNNING,
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
	list runq; // sit on runq
} task;

#define task task
#include "coapi.h"

typedef struct runq {
	pthread_cond_t cond;
	pthread_mutex_t lock;
	unsigned int count;
	list head;
} runq;

static inline runq_put(runq *q, task *t) {
	list_push(&q->head, &t->runq);
	q->count++
}

static inline task *runq_take(runq *q) {
	list *l = list_take(&q->head);
	if (l) {
		q->count--;
		return CONTAINER_OF(task, runq, l);
	}
	return NULL;
}

typedef struct ctimer {
	struct timespec expire;
	task *task;
} ctimer;

typedef struct epoll_t {
	task *task;
	int fd;
} epoll_t;


#define TF_REAPER 0x1
#define is_reaper(t) ((t->flags & TF_REAPER) == TF_REAPER)
#define is_dead(t) (t->state == DEAD)
#define is_dying(t) (t->state == DYING)
#define is_detached(t) ((t->flags & TF_DETACHED) == TF_DETACHED)

#define UNSAFE 0x1
#define RESCHED 0x2

extern task *current;

#define runnable(t) (t->state == RUNNABLE || t->state == NEW)

#define for_each_task(t, link, head) \
	list_for_each(t, link, head)

#endif /* SRC_CORO_H_ */
