# A toy coroutine runtime for C

This is a toy coroutine runtime(aka, lightweight threads) for C in Linux/x64.
 
### example

The folloing code spawns 10000 coroutines that run concurrently.

```c
#include "coro.h"

static void *func(void *a) {
	int i = 0;
	while (i < 50) {
		printf("%d => %d\n", (int)a, i);
		i++;
	}
	return a;
}

void *start(void *arg) {
#define N 10000
	task *a[N];
	for (int i = 0; i < N; ++i) {
		char n[10];
		co_snprintf(&n, 10, "task%d", i);
		a[i] = coro(func, i, &n);
	}
	for (int i = 0; i < N; ++i) {
		void *r = wait_for(a[i]);
		co_printf("%s return:%d\n", a[i]->name, (int)r);
	}
}


int main(int argc, char *argv[]) {
	coro_start(start, NULL);
}

```

### Problems

 - currently all coroutines are executed in a single thread
 - synchronization mechanism missing
 