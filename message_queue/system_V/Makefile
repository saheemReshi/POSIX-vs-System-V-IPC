CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
# System V MQ uses no extra link library (unlike POSIX which needs -lrt)
LDFLAGS :=

SRCDIR  := mq_sysv
BINDIR  := bin
COMMON  := common

TARGETS := $(BINDIR)/pingpong $(BINDIR)/throughput $(BINDIR)/scalability

.PHONY: all clean

all: $(BINDIR) $(TARGETS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/pingpong: $(SRCDIR)/pingpong.c \
                    $(COMMON)/timing.h $(COMMON)/affinity.h $(COMMON)/stats.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BINDIR)/throughput: $(SRCDIR)/throughput.c \
                      $(COMMON)/timing.h $(COMMON)/affinity.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BINDIR)/scalability: $(SRCDIR)/scalability.c \
                       $(COMMON)/timing.h $(COMMON)/affinity.h $(COMMON)/stats.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(BINDIR)
