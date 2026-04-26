## ============================================================================
## IPC Benchmark Suite — IEEE System V Edition
##
## Mechanisms:
##   mq_sysv   — System V message queues
##   shm_sysv  — System V shared memory + semaphores
##   priority  — System V MQ priority behavior tests (mtype-based)
##
## Layout:
##   common/       timing.h  affinity.h  stats.h
##   mq_sysv/      pingpong.c  throughput.c  scalability.c
##   shm_sysv/     shm_pingpong.c
##   priority/     priority_test.c
##   analysis/     run_experiment.sh  perf_wrap.sh  analyze.py
##   bin/          compiled binaries
##   results/      CSV + figures + tables
##
## Quick start:
##   make setup_sysctl setup_perf
##   make all
##   make test
##   make experiment
##   make analyse
##
## Per-section:
##   make section1   ping-pong mq_sysv
##   make section2   ping-pong shm_sysv
##   make section3   throughput
##   make section4   scalability
##   make section5   priority tests
## ============================================================================

CC     := gcc
CFLAGS := -O2 -Wall -Wextra -D_GNU_SOURCE -I$(CURDIR)/common
LIBS   := -lm

BINDIR := bin
# RESDIR := results

PP := $(BINDIR)/pingpong
TP := $(BINDIR)/throughput
SC := $(BINDIR)/scalability
SHM := $(BINDIR)/shm_pingpong
PRIO := $(BINDIR)/priority_test

ALL_BINS := $(PP) $(TP) $(SC) $(SHM) $(PRIO)

RUNS  ?= 30
SIZES := 8 64 256 1024 4096 8192

.PHONY: all test setup_sysctl setup_perf \
	experiment experiment_quick section1 section2 section3 section4 section5 \
        perf_profile analyse run_all clean distclean help

all: $(ALL_BINS)

help:
	@grep '^##' Makefile | sed 's/^## \?//'

$(BINDIR):
	mkdir -p $(BINDIR)

$(RESDIR):
	mkdir -p $(RESDIR)/tables $(RESDIR)/figures $(RESDIR)/perf

$(PP): mq_sysv/pingpong.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(TP): mq_sysv/throughput.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(SC): mq_sysv/scalability.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(SHM): shm_sysv/shm_pingpong.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(PRIO): priority/priority_test.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

setup_sysctl:
	@echo "Current System V IPC limits:"
	@sysctl kernel.msgmax kernel.msgmnb kernel.msgmni
	sudo sysctl -w kernel.msgmax=8192
	sudo sysctl -w kernel.msgmnb=8388608
	sudo sysctl -w kernel.msgmni=1024
	@echo "System V IPC limits raised (reverts on reboot)."

# test: all
# 	@echo "=== smoke test ==="
# 	$(PP) -H
# 	$(PP) -n 200 -s 64 -c 0,0 -p same_core -r 1
# 	$(PP) -n 200 -s 64 -c 0,1 -p same_socket -r 1
# 	$(SHM) -n 200 -s 64 -c 0,1 -p same_socket -r 1
# 	$(TP) -n 1000 -s 64 -q 8 -c 0,1 -p test_q8 -r 1
# 	$(SC) -m c1 -n 2 -k 200 -s 64 -p fanin -r 1
# 	$(SC) -m c2 -n 2 -k 200 -s 64 -p pairs -r 1
# 	$(PRIO) -t 1 -n 200 -s 64 -p test -r 1
# 	$(PRIO) -t 2 -n 200 -s 64 -c 0,1 -p test -r 1
# 	$(PRIO) -t 3 -N 2 -n 200 -s 64 -p test -r 1
# 	@echo "=== smoke tests passed ==="

# _rebuild:
# 	$(MAKE) -B $(ALL_BINS)

# experiment: setup_sysctl | $(RESDIR)
# 	$(MAKE) _rebuild
# 	@echo "Starting full sweep ($(RUNS) runs per config)..."
# 	bash analysis/run_experiment.sh --runs $(RUNS)
# 	@echo "Done. Output: results/all_runs.csv"

# experiment_quick: | $(RESDIR)
# 	$(MAKE) _rebuild
# 	bash analysis/run_experiment.sh --quick

# section1: setup_sysctl | $(RESDIR)
# 	$(MAKE) _rebuild
# 	bash analysis/run_experiment.sh --runs $(RUNS) --section 1
# 	@echo "Section 1 done. results/all_runs.csv updated."

# section2: | $(RESDIR)
# 	$(MAKE) _rebuild
# 	bash analysis/run_experiment.sh --runs $(RUNS) --section 2
# 	@echo "Section 2 done. results/all_runs.csv updated."

# section3: | $(RESDIR)
# 	$(MAKE) _rebuild
# 	bash analysis/run_experiment.sh --runs $(RUNS) --section 3
# 	@echo "Section 3 done. results/all_runs.csv updated."

# section4: | $(RESDIR)
# 	$(MAKE) _rebuild
# 	bash analysis/run_experiment.sh --runs $(RUNS) --section 4
# 	@echo "Section 4 done. results/all_runs.csv updated."

# section5: | $(RESDIR)
# 	$(MAKE) _rebuild
# 	bash analysis/run_experiment.sh --runs $(RUNS) --section 5
# 	@echo "Section 5 done. results/all_runs.csv updated."

# perf_profile: setup_perf all | $(RESDIR)
# 	bash analysis/perf_wrap.sh
# 	@echo "perf profiling done. Output: results/perf_stat.csv"

# analyse: | $(RESDIR)
# 	@python3 -c "import numpy, scipy, matplotlib, pandas" 2>/dev/null || \
# 	    (echo "ERROR: pip install numpy scipy matplotlib pandas" && exit 1)
# 	python3 analysis/analyze.py \
# 	    --input  results/all_runs.csv \
# 	    --perf   results/perf_stat.csv \
# 	    --outdir results \
# 	    --format both
# 	@echo "Tables : results/tables/*.tex"
# 	@echo "Figures: results/figures/*.pdf"
# 	@echo "Report : results/stats_report.txt"

# run_all: experiment perf_profile analyse
# 	@echo "Full pipeline complete."

# clean:
# 	rm -rf $(BINDIR)
# 	@echo "Removing stale System V queue entries..."
# 	@ipcrm -Q 0x1AB1F 2>/dev/null || true
# 	@ipcrm -Q 0x1AB1B 2>/dev/null || true
# 	@ipcrm -Q 0x1AB20 2>/dev/null || true
# 	@ipcrm -Q 0x5C1000 2>/dev/null || true
# 	@ipcrm -Q 0x7A1001 2>/dev/null || true
# 	@ipcrm -Q 0x7A1002 2>/dev/null || true
# 	@ipcrm -Q 0x7A1003 2>/dev/null || true
# 	@for i in $$(seq 0 15); do \
# 	    ipcrm -Q $$((0x5C2F00 + i)) 2>/dev/null || true; \
# 	    ipcrm -Q $$((0x5C2B00 + i)) 2>/dev/null || true; \
# 	done
# 	@echo "Removing stale System V shared memory/semaphore entries..."
# 	@ipcrm -M 0x7A5000 2>/dev/null || true
# 	@ipcrm -S 0x7A5001 2>/dev/null || true
# 	@echo "Clean done."

# distclean: clean
# 	rm -rf $(RESDIR)
# 	@echo "distclean done."
