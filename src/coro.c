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
#include <limits.h>
#include <sys/mman.h>

#include "coro.h"
#include "list.h"

#define CLOCKID CLOCK_MONOTONIC
#define SIG_SCHED SIGRTMIN

#define TICK_INTERVAL 1000000
#define STACK_SIZE 1 << 20 // 1M default stack size

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
static nlist_head ctimers;
static int __timeout = INT_MAX;
static int epollfd;

int __scheduling = 0;
int __preempt = 0;

void _switch_to(task *t);

static void *alloc_stack(size_t size) {
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		err_exit("error alloc_stack");
	}
	return p;
}

static void free_stack(void *p, size_t size) {
	err_guard(munmap(p, size), "error free_stack");
}

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
	if (is_detached(t)) {
		// return value not important, dies immediately
		t->state = DEAD;
	} else {
		// someone waiting for the return value
		t->state = DYING;
	}
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
	free_stack((void *)t->stack, STACK_SIZE); // configurable size?
	free(t);

}


void __sched_cleanup() {
	free(sched);
}

void task_entry(task *t) {
	void *r = ((void *(*)(void *))t->ip)(t->arg);
	t->ret = r;
	task_exit(t);
}

void tick_enable() {
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
}

void __schedule();
void __schedule_end();

void schedule() {
	struct itimerspec its;
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	if (timer_settime(timerid, 0, &its, NULL) == -1)
		err_exit("timer_settime");
	raise(SIG_SCHED);
}

static void tick(int sig, siginfo_t *si, void *uc) {
	_sigcontext *ctx = (_sigcontext *)uc;
	if ((ctx->uc_mcontext.rip >= (unsigned long)__schedule
			&& ctx->uc_mcontext.rip < (unsigned long)__schedule_end)
			|| __scheduling) {
		return;
	}
	// not in scheduler
	if (preempt_disabled()) {
		// in non-preemptable user code
		async_preempt();
		tick_enable();
		return;
	}
	current->ctx.context = *(co_context *)&ctx->uc_mcontext;
	current->ctx.fpstate = *(co_fpstate *)ctx->uc_mcontext.fpstate;
	ctx->uc_mcontext.rip = (unsigned long)__schedule;

}

static task *new_task(void *(*f)(void *), void *arg, char *name, size_t stack_size, int flags) {
	task *t = (task*)co_calloc(1, sizeof(task));
	if (!t) {
		err_exit("unable to allocate coroutine, out of memory");
	}
	t->stack = (unsigned long)alloc_stack(stack_size);
	if (!t->stack) {
		err_exit("unable to allocate stack, out of memory");
	}
	t->sp = t->stack + (stack_size); // top of stack
	t->ip = (unsigned long)f;
	t->arg = arg;
	t->state = NEW;
	t->waitq = NLIST_INIT();
	t->flags = flags;
	if (name) {
		strncpy(&t->name, name, sizeof(t->name) - 1);
	}
	add_new(t);
	return t;
}

task *coro(void *(*f)(void *), void *a, char *name, int flags) {
	flags &= ~TF_REAPER; // reaper flag can't be changed
	return new_task(f, a, name, STACK_SIZE, flags);
}

static inline task *_get_next(task *t) {
	return t == sched || t->link.next == &tasks ? CONTAINER_OF(task, link, tasks.next) : CONTAINER_OF(task, link, t->link.next);
}


void __sched_timer(struct timespec *to) {
	ctimer ct;
	err_guard(clock_gettime(CLOCK_MONOTONIC, &ct.expire), "clock_gettime");
	ct.expire.tv_sec += to->tv_sec;
	ct.expire.tv_nsec += to->tv_nsec;
	ct.task = current;
	nlist_node __n = { NULL, NULL, NULL };
	__n.n = &ct;
	int to_milli = to->tv_sec * 1000 + to->tv_nsec / 1000000;
	preempt_disable();
	nlist_push(&__n, &ctimers);
	if (to_milli < __timeout) {
		__timeout = to_milli;
	}
	current->state = BLOCKED;
	preempt_enable();
	schedule();
}

void __sched_epoll(int fd, struct epoll_event *ev) {
	preempt_disable();
	err_guard(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, ev),
			"error epoll_ctl");
	current->state = BLOCKED;
	preempt_enable();
	schedule();
}

static void handle_epoll(int timeout) {
#define MAX_EVENTS 10
	struct epoll_event events[MAX_EVENTS];
	int nfds = epoll_wait(epollfd, events, MAX_EVENTS, timeout);
	if (nfds == -1) {
		if (errno == EINTR) {
			return;
		}
		err_exit("error epoll_wait");
	}
	for (int n = 0; n < nfds; ++n) {
		epoll_t *t = (epoll_t *)events[n].data.ptr;
		t->task->state = RUNNABLE;
		err_guard(epoll_ctl(epollfd, EPOLL_CTL_DEL, t->fd, NULL), "error epoll_ctl");
	}
}

static void handle_timers() {
	if (nlist_empty(&ctimers)) {
		__timeout = INT_MAX;
		return;
	}
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
		} else {
			int to = (t->expire.tv_sec - ts.tv_sec) * 1000 + (t->expire.tv_nsec - ts.tv_nsec) / 1000000;
			if (to < __timeout) {
				__timeout = to;
			}
		}
	}

}

static task *_pick_next() {
	if (list_empty(&tasks)) {
		goto done; // done, scheduler exit
	}
	handle_epoll(0);
	while (1) {
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
			continue; // try next
		} else if (is_reaper(t) && is_dying(t)) {
			// sched reaps the reaper
			task_cleanup(t);
			break;
		}
		handle_epoll(__timeout);
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
	if (!sched) {
		err_exit("unable to allocate scheduler, out of memory");
	}
	strncpy(sched->name, "sched", 5);
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
	sched->ip = ret_pc;
	sched->sp = ret_sp;
	// user main as arg to sched
	sched->arg = (void*)main;
	// return value of main is stored in sched, in stead of the main task
	task *m = coro(main_wrapper, arg, "main", 0);
	m->flags |= TF_REAPER;
	// never return here, this stack frame will get destroyed by the following call
	schedule();
}

