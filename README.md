# A toy coroutine runtime for C

This is a toy coroutine runtime(aka, lightweight threads) for C in Linux/x64.

### build

```bash
$ make clean all
```

### example

The following code spawns 10000 coroutines that run concurrently.

```c
#include <coapi.h>

static void *func(void *a) {
    int i = 0;
    while (i < 50) {
        co_printf("%d => %d\n", (int)a, i);
        i++;
    }
    return a;
}

void *start(void *arg) {
#define N 10000
    task *a[N];
    for (int i = 0; i < N; ++i) {
        char n[10];
        co_snprintf(n, 10, "task%d", i);
        a[i] = coro(func, i, n, 0);
    }
    for (int i = 0; i < N; ++i) {
        void *r = wait_for(a[i]);
        co_printf("%s return:%d\n", task_name(a[i]), (int)r);
    }
}

int main() {
	coro_start(start, NULL);
}

```
compile with(provided that coapi.h and libcoro.so are in cwd):

```bash
$ gcc -o test test.c -L. -lcoro -Wl,-rpath=\$ORIGIN

```

more examples can be found in the `examples` directory:

* webserver: a simple web server that could be used as a web file server, start it with

```bash
$ webserver -p 8080 -r <path>
# files under the directory <path> then can be navigated/downloaded from web browser
```

* chatserver: a simple chat server, `nc` can be used as a client
* echoserver: a server that simply echoes whatever the client sends to it

### TODO

 - multi-threading
 - synchronization
 