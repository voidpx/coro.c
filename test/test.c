#include "../src/coro.h"

static void *func(void *a) {
	int i = 0;
	while (i < 50) {
		co_printf("%d => %d\n", (int)a, i);
		i++;
	}
	return a;
}

static void *sleep_1sec(void *a) {
	struct timespec ts;
	co_clock_gettime(CLOCK_REALTIME, &ts);
	time_t t = (time_t)ts.tv_sec;
	struct tm *tp = co_localtime(&t);
#define LEN 28
	char buffer[LEN];
	co_strftime(buffer, LEN, "%Y-%m-%d %H:%M:%S", tp);
	co_printf("start sleeping for 1 sec: %s.%ld\n", buffer, ts.tv_nsec/1000000);

	struct timespec to = {1, 0};
	co_sleep(&to);
	co_clock_gettime(CLOCK_REALTIME, &ts);
	t = (time_t)ts.tv_sec;
	tp = co_localtime(&t);
	co_strftime(buffer, LEN, "%Y-%m-%d %H:%M:%S", tp);
	co_printf("end sleeping for 1 sec: %s.%ld\n", buffer, ts.tv_nsec/1000000);
	return NULL;
}

void *start(void *arg) {
#define N 1000
	task *a[N];
	for (int i = 0; i < N; ++i) {
		char n[10];
		co_snprintf(&n, 10, "task%d", i);
		a[i] = coro(func, i, &n, 0);
	}
	task *t = coro(sleep_1sec, NULL, "sleep1sec", 0);
	for (int i = 0; i < N; ++i) {
		void *r = wait_for(a[i]);
		co_printf("%s return:%d\n", a[i]->name, (int)r);
	}
	return wait_for(t);
}


int main(int argc, char *argv[]) {
	coro_start(start, NULL);
}



//void debug_dump(task *t, char *msg) {
//#undef printf
//	printf("=========task dump: %s\n", msg);
//	printf("task: %s: %p\n", t->name, t);
//	printf("sp: %#lx\n", t->sp);
//	printf("pc: %#lx\n", t->pc);
//	printf("registers:\n");
//	printf("eflags: %ld\n");
//	printf("rcx: %#lx\n", *(unsigned long *)t->sp);
//	printf("rax: %#lx\n", *(unsigned long *)(t->sp + 8));
//	printf("rdx: %#lx\n", *(unsigned long *)(t->sp + 0x10));
//	printf("rbx: %#lx\n", *(unsigned long *)(t->sp + 0x18));
//	printf("rbp: %#lx\n", *(unsigned long *)(t->sp + 0x20));
//	printf("rsi: %#lx\n", *(unsigned long *)(t->sp + 0x28));
//	printf("rdi: %#lx\n", *(unsigned long *)(t->sp + 0x30));
//	printf("r15: %#lx\n", *(unsigned long *)(t->sp + 0x38));
//	printf("r14: %#lx\n", *(unsigned long *)(t->sp + 0x40));
//	printf("r13: %#lx\n", *(unsigned long *)(t->sp + 0x48));
//	printf("r12: %#lx\n", *(unsigned long *)(t->sp + 0x50));
//	printf("r11: %#lx\n", *(unsigned long *)(t->sp + 0x58));
//	printf("r10: %#lx\n", *(unsigned long *)(t->sp + 0x60));
//	printf("r9: %#lx\n", *(unsigned long *)(t->sp + 0x68));
//	printf("r8: %#lx\n", *(unsigned long *)(t->sp + 0x70));
//	printf("rbp: %#lx\n", *(unsigned long *)(t->sp + 0x78));
//
//	printf("=========task dump end==============\n");
//
//#define printf printf
//}
//
//void debug_dump_sched_out(task *t) {
//	if (strncmp(t->name, "main", 4)
//			&& strncmp(t->name, "sleep5sec", 9)) {
//		return;
//	}
//
//	debug_dump(t, "sched out");
//}
//
//void debug_dump_sched_in(task *t) {
//	if (strncmp(t->name, "main", 4)
//				&& strncmp(t->name, "sleep5sec", 9)) {
//			return;
//		}
//	debug_dump(t, "sched in");
//	if (t->sp - t->stack == 0xFD788) {
//		printf("stack crashed\n");
//	}
//}
