## IPC Benchmark Suite — POSIX Semaphores
##
## Targets:
##   make all         — build all 3 binaries (sem_posix suite only)
##   make sem         — build sem_posix suite only
##   make test        — smoke test sem_posix suite only
##   make test_sem    — smoke test sem_posix only
##   make run_all     — full parameter sweep → results/results.csv
##   make run_sem     — sweep sem_posix only → results/results.csv
##   make clean       — remove binaries + stale kernel IPC names
##
## Binary layout:
##   bin/sem_posix/{pingpong,throughput,scalability}
##
## CSV output:
##   results/results.csv  (header written once; rows appended)

CC     := gcc
CFLAGS := -O2 -Wall -Wextra -D_GNU_SOURCE -I"$(CURDIR)/common"
LIBS   := -lrt -lm

BINDIR_SEM := bin/sem_posix
RESDIR     := results
CSV        := $(RESDIR)/results.csv

SEM_FLAG := -DMECHANISM='"sem_posix"'

SEM_BINS := $(BINDIR_SEM)/pingpong $(BINDIR_SEM)/throughput $(BINDIR_SEM)/scalability

.PHONY: all sem test test_sem run_all run_sem clean \
        _sweep_sem

# ── build ────────────────────────────────────────────────────────────────
all: sem

sem: $(SEM_BINS)

$(BINDIR_SEM):
	mkdir -p $(BINDIR_SEM)

$(RESDIR):
	mkdir -p $(RESDIR)

$(BINDIR_SEM)/pingpong: sem_posix/pingpong.c common/*.h | $(BINDIR_SEM)
	$(CC) $(CFLAGS) $(SEM_FLAG) -o $@ $< $(LIBS)

$(BINDIR_SEM)/throughput: sem_posix/throughput.c common/*.h | $(BINDIR_SEM)
	$(CC) $(CFLAGS) $(SEM_FLAG) -o $@ $< $(LIBS)

$(BINDIR_SEM)/scalability: sem_posix/scalability.c common/*.h | $(BINDIR_SEM)
	$(CC) $(CFLAGS) $(SEM_FLAG) -o $@ $< $(LIBS)

# ── smoke tests ──────────────────────────────────────────────────────────
test: test_sem

test_sem: sem
	@echo "=== smoke test: sem_posix ==="
	@$(BINDIR_SEM)/pingpong    -H
	@$(BINDIR_SEM)/pingpong    -n 200  -p same_core
	@$(BINDIR_SEM)/throughput  -n 1000 -p same_core
	@$(BINDIR_SEM)/scalability -m c1 -n 2 -k 200 -p mixed
	@$(BINDIR_SEM)/scalability -m c2 -n 2 -k 200 -p mixed
	@echo "=== sem_posix passed ==="

# ── full sweep ───────────────────────────────────────────────────────────
# run_all: fresh CSV (header + sem suite)
# run_sem: append sem rows only (writes header if CSV absent)

run_all: all | $(RESDIR)
	@$(BINDIR_SEM)/pingpong -H > $(CSV)
	@echo "Results → $(CSV)"
	@$(MAKE) --no-print-directory _sweep_sem
	@echo ""
	@echo "Done. $$(( $$(wc -l < $(CSV)) - 1 )) data rows written to $(CSV)."

run_sem: sem | $(RESDIR)
	@if [ ! -f $(CSV) ]; then $(BINDIR_SEM)/pingpong -H > $(CSV); fi
	@$(MAKE) --no-print-directory _sweep_sem
	@echo "Done. sem_posix rows appended to $(CSV)."

# ── internal sweep helpers ───────────────────────────────────────────────
_sweep_sem:
	@echo ""
	@echo "── sem_posix ───────────────────────────────────────────────"
	@echo "[sem] pingpong — signal round-trip latency"
	@for pl in same_core diff_core; do \
	    case $$pl in same_core) cf="-c 0,0";; diff_core) cf="-c 0,1";; esac; \
	    echo "  pingpong -n 10000 -p $$pl"; \
	    $(BINDIR_SEM)/pingpong -n 10000 $$cf -p $$pl >> $(CSV); \
	done
	@echo "[sem] throughput — sync ops/sec vs channel depth"
	@for depth in 1 16 64 256; do \
	    echo "  throughput -n 100000 -q $$depth"; \
	    $(BINDIR_SEM)/throughput -n 100000 -q $$depth -p depth_$$depth >> $(CSV); \
	done
	@echo "[sem] scalability C1 — semaphore lock contention vs N producers"
	@for n in 1 2 4 8; do \
	    echo "  scalability c1 -n $$n -k 5000"; \
	    $(BINDIR_SEM)/scalability -m c1 -n $$n -k 5000 -p fanin >> $(CSV); \
	done
	@echo "[sem] scalability C2 — parallel pairs vs N pairs"
	@for n in 1 2 4 8; do \
	    echo "  scalability c2 -n $$n -k 2000"; \
	    $(BINDIR_SEM)/scalability -m c2 -n $$n -k 2000 -p pairs >> $(CSV); \
	done

# ── clean ────────────────────────────────────────────────────────────────
clean:
	rm -rf bin/
	@echo "Removing stale POSIX semaphore entries..."
	@for s in ipc_sem_pp_fwd ipc_sem_pp_bwd \
	           ipc_sem_tput_slots ipc_sem_tput_items \
	           ipc_sem_c1_items \
	           ipc_sem_c2_fwd_0 ipc_sem_c2_fwd_1 \
	           ipc_sem_c2_fwd_2 ipc_sem_c2_fwd_3 \
	           ipc_sem_c2_bwd_0 ipc_sem_c2_bwd_1 \
	           ipc_sem_c2_bwd_2 ipc_sem_c2_bwd_3; do \
	    rm -f /dev/shm/sem.$$s 2>/dev/null || true; \
	done
	@echo "Clean done."
