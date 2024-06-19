CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic -fPIC
AS = as
ASFLAGS = -g
LD = ld

srcdir = src
outdir = build
$(shell mkdir -p $(outdir))

cobjs = $(patsubst $(srcdir)/%.c,$(outdir)/%.o,$(wildcard $(srcdir)/*.c)) 
asobjs = $(patsubst $(srcdir)/%.S,$(outdir)/%.o,$(wildcard $(srcdir)/*.S))
objs = $(cobjs) $(asobjs)

exampleobjs = $(outdir)/chatserver.o $(outdir)/echoserver.o
testobjs = $(outdir)/test.o

all: $(outdir)/chatserver  $(outdir)/echoserver $(outdir)/test

$(outdir)/cort.o: $(objs)
	$(LD) -r -o $@ $^

$(outdir)/libcoro.so: $(outdir)/cort.o
	$(LD) -shared --version-script=coro.ld -o $@ $^

$(cobjs): $(outdir)/%.o: src/%.c
	 $(CC) -c $(CFLAGS) $< -o $@
	 
$(asobjs): $(outdir)/%.o: src/%.S
	$(AS) $(ASFLAGS) -o $@ $<
	
$(exampleobjs): $(outdir)/%.o: examples/%.c
	 $(CC) -c $(CFLAGS) $< -o $@

$(testobjs): $(outdir)/%.o: test/%.c
	 $(CC) -c $(CFLAGS) $< -o $@
	 
$(outdir)/test: $(outdir)/test.o $(objs)
	$(CC) -o $@ $^ -lc -lpthread

$(outdir)/echoserver: $(outdir)/echoserver.o $(objs)
	$(CC) -o $@ $^ 
	
$(outdir)/chatserver: $(outdir)/chatserver.o  $(outdir)/libcoro.so
	$(CC) -o $@ $^ -lcoro  -Wl,-rpath,'$$ORIGIN' -L$(outdir)
	
test: $(outdir)/test
	python test/test.py
	
clean:
	rm -rf $(outdir)/*

.PHONY: all clean test

