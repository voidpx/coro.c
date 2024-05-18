#include "../src/coro.h"

static void *func(void *a) {
	int i = 0;
	while (i < 50) {
		printf("%d => %d\n", (int)a, i);
		i++;
	}
	return a;
}

static void *sleep_5sec(void *a) {
	struct timespec ts;
	__clock_gettime(CLOCK_REALTIME, &ts);
	time_t t = (time_t)ts.tv_sec;
	struct tm *tp = localtime(&t);
#define LEN 28
	char buffer[LEN];
	strftime(buffer, LEN, "%Y-%m-%d %H:%M:%S", tp);
	printf("start sleeping for 5 sec: %s.%ld\n", buffer, ts.tv_nsec/1000000);
	co_sleep(5);
	__clock_gettime(CLOCK_REALTIME, &ts);
	t = (time_t)ts.tv_sec;
	tp = localtime(&t);
	strftime(buffer, LEN, "%Y-%m-%d %H:%M:%S", tp);
	printf("end sleeping for 5 sec: %s.%ld\n", buffer, ts.tv_nsec/1000000);
	return NULL;
}

void *start(void *arg) {
#define N 1000
	task *a[N];
	for (int i = 0; i < N; ++i) {
		char n[10];
		snprintf(&n, 10, "task%d", i);
		a[i] = coro(func, i, &n);
	}
	task *t = coro(sleep_5sec, NULL, "sleep 5 sec");
	for (int i = 0; i < N; ++i) {
		void *r = wait_for(a[i]);
		printf("%s return:%d\n", a[i]->name, (int)r);
	}
	return wait_for(t);
}


int main(int argc, char *argv[]) {
	coro_start(start, NULL);
}
