# CC      := gcc
# CFLAGS  := -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
# # System V MQ uses no extra link library (unlike POSIX which needs -lrt)
# LDFLAGS :=

# SRCDIR  := mq_sysv
# BINDIR  := bin
# COMMON  := common

# TARGETS := $(BINDIR)/pingpong $(BINDIR)/throughput $(BINDIR)/scalability

# .PHONY: all clean

# all: $(BINDIR) $(TARGETS)

# $(BINDIR):
# 	mkdir -p $(BINDIR)

# $(BINDIR)/pingpong: $(SRCDIR)/pingpong.c \
#                     $(COMMON)/timing.h $(COMMON)/affinity.h $(COMMON)/stats.h
# 	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# $(BINDIR)/throughput: $(SRCDIR)/throughput.c \
#                       $(COMMON)/timing.h $(COMMON)/affinity.h
# 	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# $(BINDIR)/scalability: $(SRCDIR)/scalability.c \
#                        $(COMMON)/timing.h $(COMMON)/affinity.h $(COMMON)/stats.h
# 	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# clean:
# 	rm -rf $(BINDIR)
## IPC Benchmark Suite — System V Message Queues
## make all     — build all three binaries into bin/
## make test    — smoke test with small iteration counts
## make run_all — full sweep → results/results.csv
## make clean   — remove binaries and any stale System V queue entries

CC     := gcc
CFLAGS := -O2 -Wall -Wextra -D_GNU_SOURCE -I$(CURDIR)/common
# System V MQ uses no extra link library (unlike POSIX which needs -lrt)
LIBS   := -lm

BINDIR := bin
RESDIR := results

# IPC keys used in the source files (hex) — must match the #defines in each .c
KEY_FWD    := 0x1AB1F
KEY_BWD    := 0x1AB1B
KEY_TPUT   := 0x1AB20
KEY_C1     := 0x5C1000
KEY_C2_FWD := 0x5C2F00   # base; pairs use base+0 … base+7
KEY_C2_BWD := 0x5C2B00

.PHONY: all test run_all clean

all: $(BINDIR)/pingpong $(BINDIR)/throughput $(BINDIR)/scalability

$(BINDIR):
	mkdir -p $(BINDIR)

$(RESDIR):
	mkdir -p $(RESDIR)

$(BINDIR)/pingpong: mq_sysv/pingpong.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(BINDIR)/throughput: mq_sysv/throughput.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(BINDIR)/scalability: mq_sysv/scalability.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

test: all
	@echo "=== smoke test ==="
	bin/pingpong    -H
	bin/pingpong    -n 200  -s 64   -p same_core
	bin/throughput  -n 1000 -s 64   -p diff_core
	bin/scalability -m c1 -n 2 -k 200 -p mixed
	bin/scalability -m c2 -n 2 -k 200 -p mixed
	@echo "=== passed ==="

run_all: all | $(RESDIR)
	./bin/pingpong -n 20000 -s 8    >> results/pingpong.csv
	./bin/pingpong -n 20000 -s 64   >> results/pingpong.csv
	./bin/pingpong -n 20000 -s 256  >> results/pingpong.csv
	./bin/pingpong -n 20000 -s 1024 >> results/pingpong.csv
	./bin/throughput -n 100000 -s 8    -q 64 >> results/throughput.csv
	./bin/throughput -n 100000 -s 64   -q 64 >> results/throughput.csv
	./bin/throughput -n 100000 -s 256  -q 64 >> results/throughput.csv
	./bin/throughput -n 100000 -s 1024 -q 64 >> results/throughput.csv
	./bin/scalability -m c1 -n 1 -k 5000 >> results/scalability_c1.csv
	./bin/scalability -m c1 -n 2 -k 5000 >> results/scalability_c1.csv
	./bin/scalability -m c1 -n 4 -k 5000 >> results/scalability_c1.csv
	./bin/scalability -m c1 -n 8 -k 5000 >> results/scalability_c1.csv
	./bin/scalability -m c2 -n 1 -k 5000 >> results/scalability_c2.csv
	./bin/scalability -m c2 -n 2 -k 5000 >> results/scalability_c2.csv
	./bin/scalability -m c2 -n 4 -k 5000 >> results/scalability_c2.csv
	./bin/scalability -m c2 -n 8 -k 5000 >> results/scalability_c2.csv

clean:
	rm -rf $(BINDIR)
	@echo "Removing stale System V message queues (if any)..."
	@for key in $(KEY_FWD) $(KEY_BWD) $(KEY_TPUT) $(KEY_C1) \
	            $(KEY_C2_FWD) $$(($(KEY_C2_FWD)+1)) $$(($(KEY_C2_FWD)+2)) $$(($(KEY_C2_FWD)+3)) \
	            $$(($(KEY_C2_FWD)+4)) $$(($(KEY_C2_FWD)+5)) $$(($(KEY_C2_FWD)+6)) $$(($(KEY_C2_FWD)+7)) \
	            $(KEY_C2_BWD) $$(($(KEY_C2_BWD)+1)) $$(($(KEY_C2_BWD)+2)) $$(($(KEY_C2_BWD)+3)) \
	            $$(($(KEY_C2_BWD)+4)) $$(($(KEY_C2_BWD)+5)) $$(($(KEY_C2_BWD)+6)) $$(($(KEY_C2_BWD)+7)); do \
	    id=$$(ipcs -q | awk -v k="$$key" '$$3 == k {print $$2}'); \
	    if [ -n "$$id" ]; then ipcrm -q $$id && echo "  removed msqid=$$id (key=$$key)"; fi; \
	done
	@echo "Done."