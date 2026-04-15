## IPC Benchmark Suite — IEEE Extended Edition
##
## Mechanisms:
##   mq_posix  — POSIX message queues
##   shm_posix — POSIX shared memory + semaphores (copy-free baseline)
##   priority  — POSIX MQ message priority (novel contribution)
##
## Layout:
##   common/       timing.h  affinity.h  stats.h
##   mq_posix/     pingpong.c  throughput.c  scalability.c
##   shm_posix/    shm_pingpong.c
##   priority/     priority_test.c
##   analysis/     run_experiment.sh  perf_wrap.sh  analyze.py
##   bin/          compiled binaries  (make all)
##   results/      CSV + figures      (runtime)
##
## ── Quick start ──────────────────────────────────────────────────────────────
##   make setup_sysctl setup_perf   # once per boot (requires sudo)
##   make all                       # build
##   make test                      # smoke test
##   make experiment                # full 30-run sweep (all 5 sections)
##   make analyse                   # stats + LaTeX tables + figures
##
## ── Running one section at a time ────────────────────────────────────────────
##   make section1    ping-pong POSIX MQ
##   make section2    ping-pong shared memory
##   make section3    throughput
##   make section4    scalability           ← crashed here last time
##   make section5    priority tests
##
##   Each section appends to results/all_runs.csv (header written once).
##   You can run section4 after sections 1-3 are done without re-running them.
##
## ── Other targets ────────────────────────────────────────────────────────────
##   make perf_profile              IPC/TLB perf stat sweep
##   make analyse                   statistical analysis
##   make analyse_with_sysv SYSV_CSV=path   merge teammate SysV data
##   make run_all                   experiment + perf_profile + analyse
##   make experiment_quick          3 runs, reduced counts (fast test)
##   make clean                     remove binaries + stale MQ entries
##   make distclean                 clean + remove results/
## =============================================================================

CC     := gcc
CFLAGS := -O2 -Wall -Wextra -D_GNU_SOURCE -I$(CURDIR)/common
LIBS   := -lrt -lm

BINDIR := bin
RESDIR := results

PP   := $(BINDIR)/pingpong
TP   := $(BINDIR)/throughput
SC   := $(BINDIR)/scalability
SHM  := $(BINDIR)/shm_pingpong
PRIO := $(BINDIR)/priority_test

ALL_BINS := $(PP) $(TP) $(SC) $(SHM) $(PRIO)

RUNS   := 30
SIZES  := 8 64 256 1024 4096 8192

RUN_WITH_LIMITS := sudo prlimit --msgqueue=4300000:4300000 --nofile=65536:65536 bash

.PHONY: all test setup_sysctl setup_perf \
        experiment experiment_quick \
        section1 section2 section3 section4 section5 \
        perf_profile analyse analyse_with_sysv run_all \
        clean distclean help

# ── Default ───────────────────────────────────────────────────────────────────
all: $(ALL_BINS)

help:
	@grep '^##' Makefile | sed 's/^## \?//'

# ── Directories ───────────────────────────────────────────────────────────────
$(BINDIR):
	mkdir -p $(BINDIR)

$(RESDIR):
	mkdir -p $(RESDIR)/tables $(RESDIR)/figures $(RESDIR)/perf

# ── Build rules ───────────────────────────────────────────────────────────────
$(PP): mq_posix/pingpong.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(TP): mq_posix/throughput.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(SC): mq_posix/scalability.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(SHM): shm_posix/shm_pingpong.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(PRIO): priority/priority_test.c common/timing.h common/affinity.h common/stats.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(ALL_BINS): common/timing.h common/affinity.h common/stats.h

# ── Kernel limit setup ────────────────────────────────────────────────────────
setup_sysctl:
	@echo "Current MQ limits:"
	@sysctl fs.mqueue.msg_max fs.mqueue.msgsize_max
	sudo sysctl -w fs.mqueue.msg_max=1024
	sudo sysctl -w fs.mqueue.msgsize_max=8192
	@echo "MQ limits raised. Reverts on reboot."
	@echo "To make permanent: add to /etc/sysctl.conf"
	@echo ""
	@echo "Attempting RLIMIT_MSGQUEUE raise for depth=512 large msgs..."
	-prlimit --msgqueue=4300000:4300000 --pid $$$$ 2>/dev/null && \
	    echo "  prlimit OK: RLIMIT_MSGQUEUE = 4300000 bytes" || \
	    echo "  prlimit not available — throughput.c will auto-degrade depth"

setup_perf:
	@echo "Lifting perf_event_paranoid..."
	sudo sysctl -w kernel.perf_event_paranoid=-1
	sudo sysctl -w kernel.kptr_restrict=0
	@echo "perf access enabled. Reverts on reboot."

# ── Smoke test ────────────────────────────────────────────────────────────────
test: all
	@echo "=== smoke test ==="
	@echo "--- pingpong (mq_posix) ---"
	$(PP) -H
	$(PP) -n 200 -s 64 -c 0,0 -p same_core   -r 1
	$(PP) -n 200 -s 64 -c 0,1 -p same_socket -r 1
	@echo "--- throughput ---"
	$(TP) -n 1000 -s 64 -q 8 -c 0,1 -p test_q8 -r 1
	@echo "--- scalability c1 ---"
	$(SC) -m c1 -n 2 -k 200 -s 64 -p fanin -r 1
	@echo "--- scalability c2 ---"
	$(SC) -m c2 -n 2 -k 200 -s 64 -p pairs -r 1
	@echo "--- shm_pingpong ---"
	$(SHM) -n 200 -s 64 -c 0,1 -p same_socket -r 1
	@echo "--- priority T1 ---"
	$(PRIO) -t 1 -n 200 -s 64 -p test -r 1
	@echo "--- priority T2 ---"
	$(PRIO) -t 2 -n 200 -s 64 -c 0,1 -p test -r 1
	@echo "--- priority T3 ---"
	$(PRIO) -t 3 -N 2 -n 200 -s 64 -p test -r 1
	@echo "=== all smoke tests passed ==="

# ── Rebuild helper ────────────────────────────────────────────────────────────
# Force-rebuild regardless of timestamps. Used by all experiment targets so
# re-extracting sources over old binaries always picks up the latest code.
_rebuild:
	$(MAKE) -B $(ALL_BINS)

# ── Full experiment (all 5 sections) ─────────────────────────────────────────
experiment: setup_sysctl | $(RESDIR)
	$(MAKE) _rebuild
	@echo "Starting full sweep ($(RUNS) runs per config)..."
	$(RUN_WITH_LIMITS) analysis/run_experiment.sh --runs $(RUNS)
	@sudo chown -R $(USER):$(USER) $(RESDIR)
	@echo "Done. Output: results/all_runs.csv"

experiment_quick: | $(RESDIR)
	$(MAKE) _rebuild
	$(RUN_WITH_LIMITS) analysis/run_experiment.sh --quick
	@sudo chown -R $(USER):$(USER) $(RESDIR)

# ── Per-section targets ───────────────────────────────────────────────────────
# Each section appends to results/all_runs.csv.
# The CSV header is written only if the file does not already exist,
# so running section4 after section3 is safe — no duplicate header.
#
# section1 depends on setup_sysctl (MQ limits needed for pingpong).
# section2-5 do not re-run setup_sysctl to avoid sudo prompts mid-sweep.
section1: setup_sysctl | $(RESDIR)
	$(MAKE) _rebuild
	$(RUN_WITH_LIMITS) analysis/run_experiment.sh --runs $(RUNS) --section 1
	@sudo chown -R $(USER):$(USER) $(RESDIR)
	@echo "Section 1 done. results/all_runs.csv updated."

section2: | $(RESDIR)
	$(MAKE) _rebuild
	$(RUN_WITH_LIMITS) analysis/run_experiment.sh --runs $(RUNS) --section 2
	@sudo chown -R $(USER):$(USER) $(RESDIR)
	@echo "Section 2 done. results/all_runs.csv updated."

section3: | $(RESDIR)
	$(MAKE) _rebuild
	$(RUN_WITH_LIMITS) analysis/run_experiment.sh --runs $(RUNS) --section 3
	@sudo chown -R $(USER):$(USER) $(RESDIR)
	@echo "Section 3 done. results/all_runs.csv updated."

section4: | $(RESDIR)
	$(MAKE) _rebuild
	$(RUN_WITH_LIMITS) analysis/run_experiment.sh --runs $(RUNS) --section 4
	@sudo chown -R $(USER):$(USER) $(RESDIR)
	@echo "Section 4 done. results/all_runs.csv updated."

section5: | $(RESDIR)
	$(MAKE) _rebuild
	$(RUN_WITH_LIMITS) analysis/run_experiment.sh --runs $(RUNS) --section 5
	@sudo chown -R $(USER):$(USER) $(RESDIR)
	@echo "Section 5 done. results/all_runs.csv updated."

# ── perf profiling ────────────────────────────────────────────────────────────
perf_profile: setup_perf all | $(RESDIR)
	bash analysis/perf_wrap.sh
	@echo "perf profiling done. Output: results/perf_stat.csv"

# ── Statistical analysis ──────────────────────────────────────────────────────
analyse: | $(RESDIR)
	@python3 -c "import numpy, scipy, matplotlib, pandas" 2>/dev/null || \
	    (echo "ERROR: pip install numpy scipy matplotlib pandas" && exit 1)
	python3 analysis/analyze.py \
	    --input  results/all_runs.csv \
	    --perf   results/perf_stat.csv \
	    --outdir results \
	    --format both
	@echo "Tables : results/tables/*.tex"
	@echo "Figures: results/figures/*.pdf"
	@echo "Report : results/stats_report.txt"

analyse_with_sysv: | $(RESDIR)
	python3 analysis/analyze.py \
	    --input  results/all_runs.csv \
	    --sysv   $(SYSV_CSV) \
	    --perf   results/perf_stat.csv \
	    --outdir results \
	    --format both

# ── Full pipeline ─────────────────────────────────────────────────────────────
run_all: experiment perf_profile analyse
	@echo "Full pipeline complete."
	@ls -lh results/all_runs.csv results/perf_stat.csv results/stats_report.txt 2>/dev/null || true

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BINDIR)
	@echo "Removing stale POSIX MQ entries..."
	@rm -f /dev/mqueue/ipc_pp_fwd /dev/mqueue/ipc_pp_bwd \
	       /dev/mqueue/ipc_throughput /dev/mqueue/ipc_scale_c1 \
	       /dev/mqueue/ipc_shm_pp_fwd /dev/mqueue/ipc_shm_pp_bwd \
	       /dev/mqueue/ipc_prio_t1 /dev/mqueue/ipc_prio_t2 \
	       /dev/mqueue/ipc_prio_t3 2>/dev/null || true
	@for i in $$(seq 0 15); do \
	    rm -f /dev/mqueue/ipc_scale_c2_fwd_$$i \
	           /dev/mqueue/ipc_scale_c2_bwd_$$i 2>/dev/null || true; \
	done
	@echo "Clean done."

distclean: clean
	rm -rf $(RESDIR)
	@echo "distclean done."
