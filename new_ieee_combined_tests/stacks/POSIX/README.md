# IEEE Combined Tests: POSIX IPC Benchmark Suite

This folder contains the IEEE-style benchmark harness for evaluating Linux IPC behavior under controlled, repeatable conditions.

Current scope in this folder:

- POSIX message queues (mq_posix)
- POSIX shared memory baseline (shm_posix)
- POSIX message priority stress tests (priority)

The code is organized so you can:

1. Build all benchmarks
2. Run full or per-section experiment sweeps (typically 30 runs/config)
3. Collect optional hardware counter data (perf stat)
4. Generate publication-ready tables, figures, and a statistical report

## What Is Happening In This Codebase

At a high level, the suite measures latency, throughput, scalability, and priority behavior of POSIX IPC mechanisms while controlling for CPU placement and run-to-run noise.

The pipeline is:

1. Build binaries (`make all`)
2. Run orchestrated experiments (`analysis/run_experiment.sh`, usually via `make experiment`)
3. Store raw rows in `results/all_runs.csv`
4. Optionally collect microarchitectural counters (`make perf_profile`)
5. Analyze and render results (`make analyse`)

## Folder Layout

```text
POSIX/
  Makefile                 # build + run orchestration targets
  common/
    timing.h               # nanosecond timing + percentile/stat helpers + CSV output schema
    affinity.h             # CPU pinning + topology helpers
    stats.h                # memory snapshot helpers (VmRSS deltas)
  mq_posix/
    pingpong.c             # section 1: MQ ping-pong latency
    throughput.c           # section 3: producer throughput (depth sweep)
    scalability.c          # section 4: fan-in and parallel-pairs scalability
  shm_posix/
    shm_pingpong.c         # section 2: shared-memory ping-pong baseline
  priority/
    priority_test.c        # section 5: T1/T2/T3 priority behavior tests
  analysis/
    run_experiment.sh      # full benchmark orchestrator (sections 1..5)
    perf_wrap.sh           # perf stat wrapper (IPC/cache/dTLB/context switches)
    analyze.py             # statistics + LaTeX tables + figures + report
  results/
    all_runs.csv           # raw benchmark output (all sections/runs)
    perf_stat.csv          # optional perf counter output
    stats_report.txt       # generated textual summary
    figures/               # generated plots
    tables/                # generated LaTeX tables
```

## Benchmark Sections (from `run_experiment.sh`)

### Section 1: Ping-Pong Latency (POSIX MQ)

- Binary: `bin/pingpong`
- Two processes exchange messages through two POSIX queues.
- Measures RTT each iteration and reports one-way latency as RTT/2.
- Sweeps message sizes (`8, 64, 256, 1024, 4096, 8192` bytes).
- Runs across topology placements (`same_core`, `same_socket`, `cross_socket` when available).

### Section 2: Ping-Pong Latency (Shared Memory Baseline)

- Binary: `bin/shm_pingpong`
- Uses shared memory for payload + semaphores for synchronization.
- Serves as a low-overhead baseline to isolate queue-copy/syscall overhead seen in MQ.
- Same size/placement style as section 1.

### Section 3: Throughput (POSIX MQ)

- Binary: `bin/throughput`
- One producer and one consumer; reports producer-side throughput.
- Sweeps queue depths (`1, 64, 512`) and message sizes.
- Includes queue-depth auto-degradation logic if kernel limits reject requested depth.

### Section 4: Scalability Under Contention

- Binary: `bin/scalability`
- C1 fan-in: `N producers -> 1 consumer` on a shared queue (lock/contention stress).
- C2 parallel pairs: `N` independent ping-pong pairs running concurrently.
- Reports throughput (C1) and tail latency under contention (C2).

### Section 5: Priority Tests (POSIX MQ)

- Binary: `bin/priority_test`
- T1: head-of-line jump behavior (HIGH vs LOW under concurrent stream)
- T2: latency divergence under backpressure (throttled consumer)
- T3: multi-producer start-barrier stress (priority preservation under true concurrency)

## Common Measurement Methodology

- CPU affinity pinning is used to reduce scheduling noise.
- Topology probing auto-selects representative CPU pairs.
- Multi-run design (`--runs`, default 30) captures variability.
- Core latency percentiles: p50, p95, p99, p99.9.
- Memory footprint proxy: VmRSS delta (`mem_delta_kb`).
- Results are emitted as CSV rows with a consistent schema.

## Build And Run

From `IEEE_Combined_Tests/POSIX`:

```bash
make setup_sysctl setup_perf   # one-time-per-boot kernel setup (sudo)
make all                       # build all binaries
make test                      # smoke test
make experiment                # full 5-section sweep, 30 runs/config
make perf_profile              # optional perf counter sweep
make analyse                   # generate report, tables, figures
```

Run one section at a time:

```bash
make section1
make section2
make section3
make section4
make section5
```

Quick reduced run:

```bash
make experiment_quick
```

## Key Outputs

After experiments/analysis:

- `POSIX/results/all_runs.csv`: all raw measurements
- `POSIX/results/memory_profile.csv`: non-zero memory-delta subset
- `POSIX/results/topo.txt`: topology snapshot used for placement labeling
- `POSIX/results/degradation_warnings.log`: queue-depth fallback warnings
- `POSIX/results/perf_stat.csv`: perf stat counters (if profiled)
- `POSIX/results/stats_report.txt`: textual statistical summary
- `POSIX/results/tables/*.tex`: LaTeX tables for paper inclusion
- `POSIX/results/figures/*.(pdf|png)`: generated publication figures

## Data Schema (all_runs.csv)

Primary columns emitted by benchmark binaries:

- `mechanism`
- `benchmark`
- `n_procs`
- `msg_size_bytes`
- `placement`
- `run_id`
- `iterations`
- `avg_ns, p50_ns, p95_ns, p99_ns, p999_ns, min_ns, max_ns`
- `throughput_msg_s, throughput_MB_s`
- `mem_delta_kb`

## Analysis Capabilities (`analysis/analyze.py`)

The analysis script can:

- Compute mean/std and 95% confidence intervals
- Run significance tests (Wilcoxon or Mann-Whitney, depending on pairing)
- Produce mechanism comparison tables and plots
- Generate priority-specific T1/T2/T3 summaries
- Produce NUMA placement and memory footprint tables
- Merge System V data for cross-mechanism comparison (`--sysv <csv>`) if available

Example:

```bash
python3 analysis/analyze.py \
  --input results/all_runs.csv \
  --perf results/perf_stat.csv \
  --outdir results \
  --format both
```

## Requirements

- Linux (POSIX MQ, shared memory, semaphores, perf)
- GCC + Make
- Python 3 with: `numpy`, `scipy`, `matplotlib`, `pandas`
- `perf` tool for hardware counter profiling
- Sudo access for some sysctl/perf settings

## Notes On Kernel Limits

Some experiments require tuning limits (already automated via Make targets):

- `fs.mqueue.msg_max`
- `fs.mqueue.msgsize_max`
- `RLIMIT_MSGQUEUE`
- file descriptor limits (`ulimit -n`)

If queue creation fails at larger depths/sizes, the throughput and T3 paths include depth fallback logic and emit warnings.

## Repro Tips

- Use `make clean` to remove binaries and stale named queues.
- Use `make distclean` to also remove generated results.
- Prefer running the full pipeline on an otherwise idle machine.
- Keep the same CPU topology and governor settings when comparing runs.
