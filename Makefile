CC = gcc
CFLAGS = -g -Wall 
AS = as
LD = ld

.PHONY: all

all: test echoserver

coro.o: src/coro.c src/coro.h
	$(CC) -c $(CFLAGS) -o $@ $<
	
colib.o: src/colib.c src/coro.h
	$(CC) -c $(CFLAGS) -o $@ $<

list.o: src/list.c src/list.h
	$(CC) -c $(CFLAGS) -o $@ $<

	
sched.o: src/sched.S
	$(AS) -o $@ $<
atomic.o: src/atomic.S
	$(AS) -o $@ $<
	
test.o: examples/test.c
	$(CC) -c $(CFLAGS) -o test.o $<
	
test: test.o coro.o colib.o list.o sched.o atomic.o
	$(CC) -o $@ $^ -lc
	#./$@
	
echoserver.o: examples/echoserver.c
	$(CC) -c $(CFLAGS) -o echoserver.o $<

echoserver: echoserver.o coro.o colib.o list.o sched.o atomic.o
	$(CC) -o $@ $^ -lc
	
.PHONY: clean

clean:
	rm -f *.o test echoserver