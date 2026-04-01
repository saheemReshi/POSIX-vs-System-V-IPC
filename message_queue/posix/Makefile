## IPC Benchmark Suite — POSIX Message Queues
##
## Targets:
##   make all            — build all three binaries into bin/
##   make test           — quick smoke test (sanity check only)
##   make setup_sysctl   — raise kernel MQ limits (requires sudo)
##   make run_all        — comprehensive sweep → results/posix_results.csv
##   make clean          — remove binaries and any stale mqueue entries [cite: 13, 14]
##
## Required sysctl (run 'make setup_sysctl' or see below):
##   fs.mqueue.msg_max    >= 1024   (queue depth for throughput + fanin tests)
##   fs.mqueue.msgsize_max >= 8192  (for 4096 and 8192-byte message tests) [cite: 14]
##
## CPU pinning (-c cpu0,cpu1):
##   same_core = both processes on CPU 0    → pure scheduling / context-switch cost
##   diff_core = CPU 0 vs CPU 1             → cache-line round-trip, cross-core cost
##   If your machine has only one CPU, remove the -c flags or pass -c 0,0. [cite: 14, 15]

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -D_GNU_SOURCE -I$(CURDIR)/common
LIBS    := -lrt -lm

PERFDIR := results/perf
# The hardware/kernel events we want to count
PERF_EVENTS := cs,migrations,cache-references,cache-misses,cycles,instructions

BINDIR     := bin
RESDIR     := results
POSIX_CSV  := $(RESDIR)/posix_results.csv

PP  := $(BINDIR)/pingpong
TP  := $(BINDIR)/throughput
SC  := $(BINDIR)/scalability

# ── Iteration / message counts ─────────────────────────────────────────────
# PP_ITERS : large enough for stable p99 (tail latency is the interesting metric)
# TP_MSGS  : amortises fork+queue-open overhead; keep producer loop dominant [cite: 15, 16]
# SC_K     : msgs per producer/pair in scalability tests [cite: 16]
PP_ITERS := 50000
TP_MSGS  := 200000
SC_K     := 10000

# ── Message sizes (bytes) ──────────────────────────────────────────────────
# 8     → sub-cache-line; header/signal-only IPC [cite: 16, 17]
# 64    → one cache line; typical small IPC payload [cite: 17, 18]
# 256   → moderate payload (e.g. a small struct)
# 1024  → 1 KB; command/response in a server [cite: 18, 19]
# 4096  → one page; shows copy cost inflection
# 8192  → kernel default msgsize_max ceiling [cite: 19]
SIZES := 8 64 256 1024 4096 8192

.PHONY: all test setup_sysctl run_all clean

# ── Build ──────────────────────────────────────────────────────────────────
all: $(PP) $(TP) $(SC)

$(RESDIR):
	mkdir -p $(RESDIR)

$(PERFDIR):
	mkdir -p $(PERFDIR)
$(BINDIR):
	mkdir -p $(BINDIR)

$(RESDIR):
	mkdir -p $(RESDIR)

$(PP): mq_posix/pingpong.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(TP): mq_posix/throughput.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(SC): mq_posix/scalability.c common/*.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

# ── Sysctl setup ───────────────────────────────────────────────────────────
# Raises kernel MQ limits so all test cases can run without "mq_open: Invalid
# argument" errors. Temporary — reverts on reboot. [cite: 21, 22]
setup_sysctl:
	@echo "Current limits:"
	@sysctl fs.mqueue.msg_max fs.mqueue.msgsize_max
	sudo sysctl -w fs.mqueue.msg_max=1024
	sudo sysctl -w fs.mqueue.msgsize_max=8192
	@echo "Done. Limits will revert on reboot."
	@echo "To make permanent, add to /etc/sysctl.conf." [cite: 22, 23]

# ── Smoke test ─────────────────────────────────────────────────────────────
# Runs every benchmark with tiny counts to confirm binaries work correctly.
# Prints one header + a few CSV rows; does not write any files. [cite: 23, 24]
test: all
	@echo "=== smoke test ==="
	$(PP) -H
	$(PP) -n 500  -s 64  -c 0,0 -p same_core
	$(PP) -n 500  -s 64  -c 0,1 -p diff_core
	$(TP) -n 2000 -s 64  -q 8   -c 0,1 -p diff_core_q8
	$(SC) -m c1 -n 2 -k 300 -s 64  -p fanin
	$(SC) -m c2 -n 2 -k 300 -s 64  -p pairs
	@echo "=== passed ==="

# ── Comprehensive sweep ────────────────────────────────────────────────────
# Section 1 — Ping-Pong Latency
# Section 2 — Throughput
# Section 3 — Scalability C1: Fan-in (N producers → 1 consumer)
# Section 4 — Scalability C2: Parallel pairs (N independent ping-pong pairs)

# Added setup_sysctl dependency here to enforce limits before testing
run_all: setup_sysctl all | $(RESDIR)
	@echo "Writing CSV header to $(POSIX_CSV) ..."
	$(PP) -H > $(POSIX_CSV)

	@echo ""
	@echo "============================================================"
	@echo " [1/4]  Ping-Pong Latency   ($(PP_ITERS) iterations)"
	@echo "============================================================"
	@echo "--- same core (cpu 0 → cpu 0) ---"
	@for s in $(SIZES); do \
	    printf "  pp same_core  size=%5d B ... " $$s; \
	    $(PP) -n $(PP_ITERS) -s $$s -c 0,0 -p same_core >> $(POSIX_CSV); \
	    echo "done"; \
	done
	@echo "--- cross core (cpu 0 → cpu 1) ---"
	@for s in $(SIZES); do \
	    printf "  pp diff_core  size=%5d B ... " $$s; \
	    $(PP) -n $(PP_ITERS) -s $$s -c 0,1 -p diff_core >> $(POSIX_CSV); \
	    echo "done"; \
	done

	@echo ""
	@echo "============================================================"
	@echo " [2/4]  Throughput   ($(TP_MSGS) messages, cpu 0 → cpu 1)"
	@echo "============================================================"
	@echo "--- queue depth = 1 (fully serialised) ---"
	@for s in $(SIZES); do \
	    printf "  tp q1    size=%5d B ... " $$s; \
	    $(TP) -n $(TP_MSGS) -s $$s -q 1   -c 0,1 -p diff_core_q1   >> $(POSIX_CSV); \
	    echo "done"; \
	done
	@echo "--- queue depth = 64 (moderate buffering) ---"
	@for s in $(SIZES); do \
	    printf "  tp q64   size=%5d B ... " $$s; \
	    $(TP) -n $(TP_MSGS) -s $$s -q 64  -c 0,1 -p diff_core_q64  >> $(POSIX_CSV); \
	    echo "done"; \
	done
	@echo "--- queue depth = 512 (large buffer / peak BW; needs msg_max>=512) ---"
	@for s in $(SIZES); do \
	    printf "  tp q512  size=%5d B ... " $$s; \
	    $(TP) -n $(TP_MSGS) -s $$s -q 512 -c 0,1 -p diff_core_q512 >> $(POSIX_CSV); \
	    echo "done"; \
	done

	@echo ""
	@echo "============================================================"
	@echo " [3/4]  Scalability C1 — Fan-in   ($(SC_K) msgs/producer)"
	@echo "============================================================"
	@for n in 1 2 4 8 16; do \
	    for s in 64 256 1024; do \
	        printf "  fanin  n=%2d prods  size=%5d B ... " $$n $$s; \
	        $(SC) -m c1 -n $$n -k $(SC_K) -s $$s -p fanin >> $(POSIX_CSV); \
	        echo "done"; \
	    done; \
	done

	@echo ""
	@echo "============================================================"
	@echo " [4/4]  Scalability C2 — Parallel pairs   ($(SC_K) iters/pair)"
	@echo "============================================================"
	@for n in 1 2 4 8; do \
	    for s in 64 256 1024; do \
	        printf "  pairs  n=%2d pairs   size=%5d B ... " $$n $$s; \
	        $(SC) -m c2 -n $$n -k $(SC_K) -s $$s -p pairs >> $(POSIX_CSV); \
	        echo "done"; \
	    done; \
	done

	@echo ""
	@echo "============================================================"
	@echo " Results written to: $(POSIX_CSV)"
	@echo " Rows: $$(wc -l < $(POSIX_CSV)) (1 header + $$(( $$(wc -l < $(POSIX_CSV)) - 1 )) data)"
	@echo "============================================================"

# ── Clean ──────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BINDIR)
	@echo "Removing stale POSIX MQ entries from /dev/mqueue/ ..."
	@rm -f /dev/mqueue/ipc_pp_fwd \
	       /dev/mqueue/ipc_pp_bwd \
	       /dev/mqueue/ipc_throughput \
	       /dev/mqueue/ipc_scale_c1 2>/dev/null || true [cite: 60, 61]
	@for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	    rm -f /dev/mqueue/ipc_scale_c2_fwd_$$i 2>/dev/null || true; \
	    rm -f /dev/mqueue/ipc_scale_c2_bwd_$$i 2>/dev/null || true; \
	done [cite: 61, 62, 63]
	@echo "Clean done."
	
	
# ==============================================================================
# PERF PROFILING TARGETS
# ==============================================================================
PERFDIR := results/perf
PERF_EVENTS := cs,migrations,cache-references,cache-misses,cycles,instructions

$(PERFDIR):
	mkdir -p $(PERFDIR)

.PHONY: setup_perf_sysctl run_perf_stat run_perf_record

# ── Automatically configure kernel security for perf ──
setup_perf_sysctl:
	@echo "Lifting kernel security restrictions for perf profiling..."
	sudo sysctl -w kernel.perf_event_paranoid=-1
	sudo sysctl -w kernel.kptr_restrict=0
	@echo "Kernel ready for deep profiling."

# Tier 1: Hardware Event Sweeps (The "Broad Net")
# Notice we added 'setup_perf_sysctl' to the dependency list below!
run_perf_stat: setup_sysctl setup_perf_sysctl all | $(PERFDIR)
	@echo "Sweeping hardware events (Context Switches, Cache Misses)..."
	@rm -f $(PERFDIR)/stat_pingpong.log $(PERFDIR)/stat_throughput.log $(PERFDIR)/stat_scalability.log
	
	@echo "Profiling Ping-Pong..."
	@for s in 64 8192; do \
		echo "--- Same Core, Size $$s ---" >> $(PERFDIR)/stat_pingpong.log; \
		perf stat -e $(PERF_EVENTS) -a -o $(PERFDIR)/stat_pingpong.log --append ./bin/pingpong -n 50000 -s $$s -c 0,0 -p same_core; \
		echo "--- Cross Core, Size $$s ---" >> $(PERFDIR)/stat_pingpong.log; \
		perf stat -e $(PERF_EVENTS) -a -o $(PERFDIR)/stat_pingpong.log --append ./bin/pingpong -n 50000 -s $$s -c 0,1 -p diff_core; \
	done

	@echo "Profiling Throughput..."
	@for s in 64 8192; do \
		echo "--- Diff Core, Size $$s, Depth 64 ---" >> $(PERFDIR)/stat_throughput.log; \
		perf stat -e $(PERF_EVENTS) -a -o $(PERFDIR)/stat_throughput.log --append ./bin/throughput -n 100000 -s $$s -q 64 -c 0,1 -p diff_core; \
	done

	@echo "Profiling Fan-in Scalability..."
	@for n in 1 8; do \
		echo "--- Fan-in, $$n Producers, Size 64 ---" >> $(PERFDIR)/stat_scalability.log; \
		perf stat -e $(PERF_EVENTS) -a -o $(PERFDIR)/stat_scalability.log --append ./bin/scalability -m c1 -n $$n -k 5000 -s 64 -p fanin; \
	done
	@echo "Perf stat sweeps complete! Check $(PERFDIR)/"

# Tier 2: Kernel Call Graphs (The "Smoking Gun")
# Notice we added 'setup_perf_sysctl' here too!
run_perf_record: setup_sysctl setup_perf_sysctl all | $(PERFDIR)
	@echo "Recording kernel stack traces for worst-case bottlenecks..."
	
	@echo "1. High Lock Contention (Fan-in, 8 producers)..."
	perf record -g -o $(PERFDIR)/record_fanin_8prod.data ./bin/scalability -m c1 -n 8 -k 10000 -s 64 -p fanin
	
	@echo "2. Large Memory Copy Overhead (Throughput, 8KB)..."
	perf record -g -o $(PERFDIR)/record_throughput_8kb.data ./bin/throughput -n 100000 -s 8192 -q 64 -c 0,1 -p diff_core
	
	@echo "Done! View with: perf report -g -i $(PERFDIR)/<filename>.data"
