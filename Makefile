CC = gcc
CFLAGS = -g -Wall -fPIC
AS = as
LD = ld

srcdir = src
outdir = build
$(shell mkdir -p $(outdir))

cobjs = $(patsubst $(srcdir)/%.c,$(outdir)/%.o,$(wildcard $(srcdir)/*.c)) 
asobjs = $(patsubst $(srcdir)/%.S,$(outdir)/%.o,$(wildcard $(srcdir)/*.S))
objs = $(cobjs) $(asobjs)

exampleobjs = $(outdir)/chatserver.o $(outdir)/echoserver.o $(outdir)/webserver.o
testobjs = $(outdir)/test.o

all: $(outdir)/webserver $(outdir)/chatserver  $(outdir)/echoserver $(outdir)/test

$(outdir)/cort.o: $(objs)
	$(LD) -r -o $@ $^

$(outdir)/libcoro.so: $(outdir)/cort.o
	$(LD) -shared --version-script=coro.ld -o $@ $^ -lc

$(cobjs): $(outdir)/%.o: src/%.c
	 $(CC) -c $(CFLAGS) $< -o $@
	 
$(asobjs): $(outdir)/%.o: src/%.S
	$(AS) -o $@ $<
	
$(exampleobjs): $(outdir)/%.o: examples/%.c
	 $(CC) -c $(CFLAGS) $< -o $@

$(testobjs): $(outdir)/%.o: test/%.c
	 $(CC) -c $(CFLAGS) $< -o $@
	 
$(outdir)/test: $(outdir)/test.o $(objs)
	$(CC) -o $@ $^ -lc

$(outdir)/echoserver: $(outdir)/echoserver.o $(objs)
	$(CC) -o $@ $^ 
	
$(outdir)/chatserver: $(outdir)/chatserver.o  $(outdir)/libcoro.so
	$(CC) -o $@ $^ -lcoro  -Wl,-rpath,'$$ORIGIN' -L$(outdir)
	
$(outdir)/webserver: $(outdir)/webserver.o $(objs)
	$(CC) -o $@ $^ 
	
test: $(outdir)/test
	python test/test.py
	
clean:
	rm -rf $(outdir)/*

.PHONY: all clean test

