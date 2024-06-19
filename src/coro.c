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
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <assert.h>

#include "coro.h"
#include "list.h"

#define MAX_STACK (1 << 20)
#define PAGE_SIZE (1 << 12)

#define SIG_SCHED SIGRTMIN
#define CLOCKID CLOCK_MONOTONIC
#define TICK_INTERVAL 1000000

typedef struct _sigcontext {
	unsigned long	  uc_flags;
	struct _sigcontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	// don't care
	//sigset_t	  uc_sigmask;
} _sigcontext;

typedef struct tstate {
	int __scheduling;
	int __preempt;
	task *cur_task;
	task *sched_task;
	runq runq;
	pthread_t pth;
	unsigned long tid;
	int park;
	int reserved;
	stack_t old_sigstack;

} tstate;

static __thread int toffset;

unsigned int n_tstates = 0;
tstate *tstates;

//task *current;
//task *sched;
//int __scheduling=0;
//int __preempt = 0;

static pthread_mutex_t tasks_lock;
static list tasks = INIT_LIST(tasks); // all tasks
//static list runq = INIT_LIST(runq); // runnable tasks
//static pthread_mutex_t runq_lock;

static timer_t timerid;
static pthread_mutex_t ctimes_lock;
static nlist_head ctimers;
static int __timeout = INT_MAX;
static int epollfd;

//static stack_t old_sigstack;

const char const *task_name(task *t) {
	return t->name;
}

void preempt_disable() {
	__preempt |= UNSAFE;
}

void preempt_enable() {
	__preempt &= ~UNSAFE;
	if (__preempt & RESCHED) {
		__preempt &= ~RESCHED;
		schedule();
	}
}
#define preempt_disabled() (__preempt & UNSAFE)
#define async_preempt() __preempt |= RESCHED

void _switch_to(task *t);

long atomic_xaddl(long *p, long val);
int atomic_casl(long *p, long val, long new);

static void *alloc_stack(size_t size) {
	preempt_disable();
	if (size == 0 || (size & (PAGE_SIZE - 1)) != 0) {
		err_exit("invalid stack size");
	}
	size_t addr;
	static size_t stack_alloc = 0;
#define INITIAL_STACK 0x600000000000L
	if (atomic_casl(&stack_alloc, 0, INITIAL_STACK + MAX_STACK)) {
		addr = INITIAL_STACK;
	} else {
		addr = atomic_xaddl(&stack_alloc, MAX_STACK);
	}
	// reserve
	void *pr = mmap((void*)addr, MAX_STACK, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (pr == MAP_FAILED) {
		err_exit("error alloc_stack");
	}
	void *p = mmap((void*)(addr + MAX_STACK) - size, size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		err_exit("error alloc_stack");
	}
	preempt_enable();
	return pr;
}

static void free_stack(void *p, size_t size) {
	err_guard(munmap(p, size), "error free_stack");
}

static inline void remove_task(task *t) {
	pthread_mutex_lock(&tasks_lock);
	list_remove(&t->link);
	pthread_mutex_unlock(&tasks_lock);
}

static inline void add_new(task *t) {
	preempt_disable();
	pthread_mutex_lock(&tasks_lock);
	list_push(&t->link, &tasks);
	pthread_mutex_unlock(&tasks_lock);
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
	free_stack((void *)t->stack, MAX_STACK); // configurable size?
	free(t);

}

static void restore_sigstack();

void __sched_cleanup() {
	restore_sigstack();
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

void tick_disable() {
	struct itimerspec its;
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	if (timer_settime(timerid, 0, &its, NULL) == -1)
			err_exit("timer_settime");
}

void sched_exit() {
	tick_enable();
}

void __schedule();
void __schedule_end();

void schedule() {
	void tick_disable();
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
	ctx->uc_mcontext.rdi = &tstate;

}

static void put_on_runq(task *t) {
	runq *q = &tstates[1].runq; // 0 is the interrupter
	for (int i = 2; i < n_tstates; ++i) {
		if (q->count > tstates[i].runq.count) {
			q = &tstates[i].runq;
		}
	}
	pthread_mutex_lock(&q->lock);
	runq_put(q, t);
	pthread_cond_signal(&q->cond);
	pthread_mutex_unlock(&q->lock);
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
	t->sp = t->stack + MAX_STACK; // top of stack
	t->stack_last_mapped = t->sp - stack_size;
	t->ip = (unsigned long)f;
	t->arg = arg;
	t->state = NEW;
	t->waitq = NLIST_INIT();
	t->flags = flags;
	if (name) {
		strncpy(&t->name, name, sizeof(t->name) - 1);
	}
	add_new(t);
	put_on_runq(t);
	return t;
}

task *coro(void *(*f)(void *), void *a, char *name, int flags) {
	flags &= ~TF_REAPER; // reaper flag can't be changed
	return new_task(f, a, name, PAGE_SIZE, flags);
}

//static inline task *_get_next(task *t) {
//	return t == sched || t->link.next == &tasks ? CONTAINER_OF(task, link, tasks.next) : CONTAINER_OF(task, link, t->link.next);
//}

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
	pthread_mutex_lock(&ctimes_lock);
	nlist_push(&__n, &ctimers);
	if (to_milli < __timeout) {
		__timeout = to_milli;
	}
	current->state = BLOCKED;
	pthread_mutex_unlock(&ctimes_lock);
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

static task *pick_next() {
	tstate *t = tstates[toffset];
	pthread_mutex_lock(&t->runq.lock);
	if (t->cur_task->state == RUNNING) { // preempted
		t->cur_task->state = RUNNABLE; // about to be switched out
		// put it at the end of the run queue
		runq_put(&t->runq, t->cur_task);
	} // otherwise voluntary switch, state was already set
	task *next = runq_take(&t->runq);
	if (!next) {
		next = t->sched;
	}
	next->state = RUNNING;
	pthread_mutex_unlock(&t->runq.lock);
	return next;
}

/*
 * on scheduler stack
 */
void _schedule() {
	task *t = pick_next();

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

static void setup_sigstack() {
	size_t size = PAGE_SIZE;
	void *p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (!p) {
		err_exit("error allocating sig stack");
	}
	stack_t os;
	os.ss_sp = p;
	os.ss_size = size;
	os.ss_flags = 0;
	err_guard(sigaltstack(&os, &tstates[toffset]), "error setup_sigstack");
}

static void restore_sigstack() {
	stack_t os;
	err_guard(sigaltstack(&old_sigstack, &os), "error restore_sigstack");
	err_guard(munmap(os.ss_sp, os.ss_size), "error restore_sigstack");
}

static void setup_sched_tick() {
	struct sigaction sa;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sa.sa_sigaction = tick;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIG_SCHED, &sa, NULL) == -1)
		err_exit("sigaction: error setting up coroutine runtime");

}

static void setup_timer() {
	struct sigevent sev;
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIG_SCHED;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCKID, &sev, &timerid) == -1)
		err_exit("timer_create: error setting up coroutine runtime");
}

static void stack_mem_handler(int sig, siginfo_t *si, void *uc) {
	unsigned long addr = (unsigned long)si->si_addr;
	if (addr < current->stack + PAGE_SIZE) {
		err_exit("stack overflow");
	}
	if (addr >= current->stack_last_mapped) {
		err_exit("runtime corrupted");
	}
	addr &= ~(PAGE_SIZE - 1);
	void *p = mmap(addr, current->stack_last_mapped - addr, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		err_exit("error alloc_stack");
	}
	current->stack_last_mapped = addr;
}

static void setup_mem_handler() {
	struct sigaction sa;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sa.sa_sigaction = stack_mem_handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIG_SCHED);
	if (sigaction(SIGSEGV, &sa, NULL) == -1)
		err_exit("sigaction: error setting up memory handler");

}

static tstate *get_tstate() {
	return &tstates[toffset];
}
#define current get_tstate()->cur_task;

static void worker_unpack(int no) {
	tstate *s = &tstates[no];

}

static void init_sched(int n, unsigned long ip, unsigned long sp) {
	toffset = n;
	setup_sigstack();
	setup_mem_handler();
	task *sched = (task *)calloc(1, sizeof(task));
	if (!sched) {
		err_exit("unable to allocate scheduler, out of memory");
	}
	snprintf(sched->name, sizeof(sched->name), "sched%d", n);
	sched->state = RUNNABLE;
	sched->ip = ip;
	sched->sp = sp;
	tstates[toffset].sched_task = sched;
	tstates[toffset].cur_task = sched;
}

static void worker_loop() {
	while (1) {
		tstate *s = &tstates[toffset];
		pthread_mutex_lock(&s->runq.lock);
		while (!s->runq.count) {
			pthread_cond_wait(&s->runq.cond, &s->runq.lock, NULL);
		}
		pthread_mutex_unlock(&s->runq.lock);
		// work available
		schedule();
	}
}

static void worker(void *a) {
	int _t __attribute__((aligned(16))) = 0;
	init_sched((int)i, worker_loop, &_t);
	worker_loop();
}


void _coro_start(void *(*main)(void*), void *arg, unsigned long ret_pc, unsigned long ret_sp) {
	pthread_mutex_init(&tasks_lock);
	pthread_mutex_init(&ctimers_lock);
#ifdef MAX_PROC
	n_tstates = MAX_PROC;
	assert(n > 0);
#else
	n_tstates = get_nprocs();
#endif
	tstates = calloc(n_tstates, sizeof(tstate));
	for (int i = 0; i < n_tstates; ++i) {
		pthread_cond_init(&tstates[i].runq.cond, NULL);
	}
	if (!tstates) {
		err_exit("OOM");
	}
	init_sched(0, ret_pc, ret_sp);
	// user main as arg to sched
	tstates[toffset].sched_task->arg = (void*)main;
	// workers
	for (int i = 1; i < n_tstates; ++i) {
		tstate *s = &tstates[i];
		if (pthread_create(&s->pth, NULL, worker, NULL)){
			err_exit("error creating worker");
		}
	}

	setup_sched_tick();
	setup_timer();
	setup_epoll();
	// return value of main is stored in sched, in stead of the main task
	task *m = coro(main_wrapper, arg, "main", 0);
	m->flags |= TF_REAPER;
	// never return here, this stack frame will get destroyed by the following call
	schedule();
}

