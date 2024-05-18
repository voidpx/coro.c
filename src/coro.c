#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/ucontext.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/epoll.h>

#include "coro.h"
#include "list.h"

#define CLOCKID CLOCK_MONOTONIC
#define SIG_SCHED SIGRTMIN

#define TICK_INTERVAL 1000000
#define STACK_SIZE 1 << 20 // 2M default stack size

#define TF_REAPER 0x1
#define is_reaper(t) (t->flags & TF_REAPER)
#define is_dead(t) (t->state == DEAD)
#define is_dying(t) (t->state == DYING)

typedef struct _sigcontext {
	unsigned long	  uc_flags;
	struct _sigcontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	// don't care
	//sigset_t	  uc_sigmask;
} _sigcontext;

list tasks = INIT_LIST(tasks);
task *current;
task *sched;
static timer_t timerid;
nlist_head ctimers;
int epollfd;

static int __scheduling = 0;
static int __tick_enabled = 0;

void _switch_to(task *t);

static inline void remove_task(task *t) {
	list_remove(&t->link);
}

static inline void add_new(task *t) {
	preempt_disable();
	list_push(&t->link, &tasks);
	preempt_enable();
}

void task_exit(task *t) {
	preempt_disable();
	t->state = DYING;
	task *temp;
	nlist_for_each(temp, &t->waitq) {
		temp->state = RUNNABLE;
	}
	preempt_enable();
	// done
	schedule();
}

// on scheduler stack
void task_cleanup(task *t) {
	remove_task(t);
	free((void *)t->stack);
	free(t);

}

void __sched_exit() {
	free(sched);
}

void task_entry(task *t) {
	void *r = ((void *(*)(void *))t->pc)(t->arg);
	t->ret = r;
	task_exit(t);
}

static void tick_enable() {
	struct itimerspec its;
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = TICK_INTERVAL;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	if (timer_settime(timerid, 0, &its, NULL) == -1)
		err_exit("timer_settime: error arm sched timer");
}

void sched_exit() {
	tick_enable();
	__tick_enabled = 1;
	__scheduling = 0;
}

void sched_enter() {
	__scheduling = 1;
	if (__tick_enabled) {
		struct itimerspec its;
		its.it_value.tv_sec = 0;
		its.it_value.tv_nsec = 0;
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		if (timer_settime(timerid, 0, &its, NULL) == -1)
			err_exit("timer_settime: error shutting down");
	}
}

static void tick(int sig, siginfo_t *si, void *uc) {
	if (preempt_disabled()) {
		async_preempt();
		tick_enable();
		return;
	}
	if (__scheduling) {
		return;
	}
	__tick_enabled = 0;
	_sigcontext *ctx = (_sigcontext *)uc;
	long sp = ctx->uc_mcontext.rsp;
	sp -= sizeof(void *);
	*(unsigned long *)sp = ctx->uc_mcontext.rip;
	ctx->uc_mcontext.rip = (unsigned long)schedule;
	ctx->uc_mcontext.rsp = sp;


}

static task *new_task(void *(*f)(void *), void *arg, char *name, size_t stack_size) {
	task *t = (task*)calloc(1, sizeof(task));
	t->stack = (unsigned long)malloc(stack_size); // 2M stack
	if (!t->stack) {
		err_exit("unable to allocate stack, out of memory");
	}
	t->sp = t->stack + (stack_size); // top of stack
	t->pc = (unsigned long)f;
	t->arg = arg;
	t->state = NEW;
	t->waitq = NLIST_INIT();
	if (name) {
		strncpy(&t->name, name, sizeof(t->name) - 1);
	}
	add_new(t);
	return t;
}

task *coro(void *(*f)(void *), void *a, char *name) {
	return new_task(f, a, name, STACK_SIZE);
}

static inline task *_get_next(task *t) {
	return t == sched || t->link.next == &tasks ? CONTAINER_OF(task, link, tasks.next) : CONTAINER_OF(task, link, t->link.next);
}

static void handle_epoll() {
#define MAX_EVENTS 10
	struct epoll_event events[MAX_EVENTS];
	int nfds = err_guard(epoll_wait(epollfd, events, MAX_EVENTS, 0), "error epoll_eait");
	for (int n = 0; n < nfds; ++n) {
		epoll_t *t = (epoll_t *)events[n].data.ptr;
		t->task->state = RUNNABLE;
		err_guard(epoll_ctl(epollfd, EPOLL_CTL_DEL, t->fd, NULL), "error epoll_ctl");
	}
}

static void handle_timers() {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		err_exit("handler_timer");
	}
	ctimer *t;
	nlist_for_each_n(t, n, &ctimers) {
		if (ts.tv_sec > t->expire.tv_sec
				|| (ts.tv_sec == t->expire.tv_sec && ts.tv_nsec >= t->expire.tv_nsec)) {
			t->task->state = RUNNABLE;
			nlist_remove(n, &ctimers);
		}
	}

}

static task *_pick_next() {
	if (list_empty(&tasks)) {
		goto done; // done, scheduler exit
	}
	while (1) {
		handle_epoll();
		handle_timers();
		task *t = _get_next(current);
		task *p = t;
		while (!runnable(t)) {
			task *next = _get_next(t);
			if (is_dead(t) && t != p) {
				task_cleanup(t); // reap
			}
			t = next;
			if (t == p) {
				break; // exhausted
			}
		}
		if (runnable(t)) {
			return t;
		}
		if (!is_reaper(t) && is_dead(t)) {
			task_cleanup(t);
		} else if (is_reaper(t) && is_dying(t)) {
			// sched reaps the reaper
			task_cleanup(t);
			break;
		}
		tick_enable();
		pause();
	}
done:
	return sched;
}

/*
 * on scheduler stack
 */
void _schedule() {
	task *t = _pick_next();
	_switch_to(t);

}

void *wait_for(task *t) {
	while (t->state != DYING) {
		preempt_disable();
		if (t->state == DYING) {
			preempt_enable();
			break;
		}
		current->state = BLOCKED;
		nlist_node n = {NULL, NULL, current};
		nlist_push(&n, &t->waitq);
		preempt_enable();
		schedule();
		nlist_remove(&n, &t->waitq);
	}
	void *r = t->ret;
	t->state = DEAD;
	return r;
}

static void *main_wrapper(void *arg) {
	void *r = ((void *(*)(void *))sched->arg)(arg);
	sched->ret = r;
	return NULL;
}

static void setup_epoll() {
	epollfd = epoll_create1(0);
	 if (epollfd == -1) {
	   err_exit("error setting up epoll");
   }

}

void _coro_start(void *(*main)(void*), void *arg, unsigned long ret_pc, unsigned long ret_sp) {
	struct sigevent sev;
	struct sigaction sa;
	sched = (task *)calloc(1, sizeof(task));
	sched->state = RUNNABLE;
	current = sched;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = tick;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIG_SCHED, &sa, NULL) == -1)
		err_exit("sigaction: error setting up coroutine runtime");

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIG_SCHED;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCKID, &sev, &timerid) == -1)
		err_exit("timer_create: error setting up coroutine runtime");
	setup_epoll();
	sched->pc = ret_pc;
	sched->sp = ret_sp;
	// XXX: dirty
	sched->arg = (void*)main;
	// return value of main is stored in sched, in stead of the main task
	task *m = coro(main_wrapper, arg, "main");
	m->flags |= TF_REAPER;
	// never return here, this stack frame will get destroyed by the following call
	schedule();
}
