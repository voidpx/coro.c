CC = gcc
CFLAGS = -g -Wall 
AS = as
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
	$(CC) -o $@ $^ -lc
	
$(outdir)/chatserver: $(outdir)/chatserver.o $(objs)
	$(CC) -o $@ $^ -lc
	
test: $(outdir)/test
	python test/test.py
	
clean:
	rm -rf $(outdir)/*

.PHONY: all clean test

