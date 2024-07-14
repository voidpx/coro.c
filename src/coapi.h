/*
 * coroutine API
 */
#ifndef SRC_COAPI_H_
#define SRC_COAPI_H_

#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#ifndef task
#define task void
#endif

#define err_exit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                               } while (0)
#define err_out(fmt,...) do{fprintf(stderr, fmt, __VA_ARGS__);}while(0)

#define err_guard(call, msg) ({int __ret = (call);if (__ret == -1) err_exit(msg); __ret;})

#define TF_DETACHED 0x2
/*
 * call into the scheduler, i.e. yield.
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

const char const *task_name(task *t);

void preempt_disable();
void preempt_enable();

#define call_no_preempt(func, rtype, ...) \
		({ \
			preempt_disable();\
			rtype __ret; \
			__ret = func(__VA_ARGS__); \
			preempt_enable();\
			__ret;})

#define call_no_preempt_void(func, ...) \
		({ \
			preempt_disable();\
			func(__VA_ARGS__); \
			preempt_enable();\
			})

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


#define co_readdir(dir) \
  ({ \
	call_no_preempt(readdir, struct dirent *, dir);})

#define co_fread(buf, es, ne, f) \
  ({ \
	call_no_preempt(fread, int, buf, es, ne, f);})


// lib functions
void co_sleep(struct timespec *ts);
int co_socket(int domain, int type, int proto);
int co_accept(int fd, struct sockaddr * addr, socklen_t * len);
ssize_t co_read(int fd, void* buf, size_t n);
ssize_t co_write(int fd, const void *buf, size_t n);

// more to be added...

#endif /* SRC_COAPI_H_ */
