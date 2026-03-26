## IPC Benchmark Suite — POSIX Message Queues
## make all     — build all three binaries into bin/
## make test    — smoke test with small iteration counts
## make run_all — full sweep → results/results.csv
## make clean   — remove binaries and any stale mqueue entries

CC     := gcc
CFLAGS := -O2 -Wall -Wextra -D_GNU_SOURCE -I$(CURDIR)/common
LIBS   := -lrt -lm

BINDIR := bin
RESDIR := results

.PHONY: all test run_all clean

all: $(BINDIR)/pingpong $(BINDIR)/throughput $(BINDIR)/scalability

$(BINDIR):
	mkdir -p $(BINDIR)

$(RESDIR):
	mkdir -p $(RESDIR)

$(BINDIR)/pingpong: mq_posix/pingpong.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(BINDIR)/throughput: mq_posix/throughput.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(BINDIR)/scalability: mq_posix/scalability.c common/*.h | $(BINDIR)
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
	bash scripts/run_all.sh

clean:
	rm -rf $(BINDIR)
	@for q in ipc_bench_pp_fwd ipc_bench_pp_bwd \
	          ipc_bench_throughput ipc_bench_c1 \
	          ipc_bench_c2_fwd_0 ipc_bench_c2_fwd_1 \
	          ipc_bench_c2_fwd_2 ipc_bench_c2_fwd_3 \
	          ipc_bench_c2_bwd_0 ipc_bench_c2_bwd_1 \
	          ipc_bench_c2_bwd_2 ipc_bench_c2_bwd_3; do \
	    rm -f /dev/mqueue/$$q 2>/dev/null || true; done
