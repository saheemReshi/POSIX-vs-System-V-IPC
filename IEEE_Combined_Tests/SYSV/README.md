# IEEE Combined Tests: System V IPC Benchmark Suite

This folder contains the IEEE-style benchmark harness for evaluating Linux IPC behavior using System V message queues under controlled, repeatable conditions.

Current scope in this folder:

- System V message queues (mq_sysv)
- System V shared memory baseline (shm_sysv)
- System V message-priority behavior tests (priority)

The code is organized so you can:

1. Build all benchmarks
2. Run full or per-section experiment sweeps (typically 30 runs/config)
3. Collect optional hardware counter data (perf stat)
4. Generate publication-ready tables, figures, and a statistical report

## What Is Happening In This Codebase

At a high level, this suite mirrors the POSIX IEEE workflow but executes the benchmark families on System V queues (`msgget`, `msgsnd`, `msgrcv`, `msgctl`) so results can be compared directly against POSIX data.

The pipeline is:

1. Build binaries (`make all`)
2. Run orchestrated experiments (`analysis/run_experiment.sh`, usually via `make experiment`)
3. Store raw rows in `results/all_runs.csv`
4. Optionally collect microarchitectural counters (`make perf_profile`)
5. Analyze and render results (`make analyse`)

## Folder Layout

```text
SYSV/
  Makefile                 # build + run orchestration targets
  common/
    timing.h               # nanosecond timing + percentile/stat helpers + CSV output schema
    affinity.h             # CPU pinning + topology helpers
    stats.h                # memory snapshot helpers (VmRSS deltas)
  mq_sysv/
    pingpong.c             # section 1: System V ping-pong latency
    throughput.c           # section 3: producer throughput (depth sweep)
    scalability.c          # section 4: fan-in and parallel-pairs scalability
  shm_sysv/
    shm_pingpong.c         # section 2: shared-memory ping-pong baseline
  priority/
    priority_test.c        # section 5: T1/T2/T3 priority behavior tests
  analysis/
    run_experiment.sh      # benchmark orchestrator (sections 1..5)
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

### Section 1: Ping-Pong Latency (System V MQ)

- Binary: `bin/pingpong`
- Two processes exchange messages through two System V queues.
- Measures RTT each iteration and reports one-way latency as RTT/2.
- Sweeps message sizes (`8, 64, 256, 1024, 4096, 8192` bytes).
- Runs across topology placements (`same_core`, `same_socket`, `cross_socket` when available).

### Section 2: Ping-Pong Latency (Shared Memory Baseline)

- Binary: `bin/shm_pingpong`
- Uses System V shared memory for payload + System V semaphores for synchronization.
- Serves as a low-overhead baseline to isolate queue-copy/syscall overhead seen in MQ.
- Same size/placement style as section 1.

### Section 3: Throughput (System V MQ)

- Binary: `bin/throughput`
- One producer and one consumer; reports producer-side throughput.
- Sweeps queue depths (`1, 64, 512`) and message sizes.
- Uses `msg_qbytes` configuration to control effective queue depth.

### Section 4: Scalability Under Contention (System V MQ)

- Binary: `bin/scalability`
- C1 fan-in: `N producers -> 1 consumer` on a shared queue.
- C2 parallel pairs: `N` independent ping-pong pairs running concurrently.
- Reports throughput (C1) and tail latency under contention (C2).

### Section 5: Priority Tests (System V MQ)

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
- Results are emitted as CSV rows with a schema compatible with the POSIX suite.

## Build And Run

From `IEEE_Combined_Tests/SYSV`:

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

- `SYSV/results/all_runs.csv`: all raw measurements
- `SYSV/results/memory_profile.csv`: non-zero memory-delta subset
- `SYSV/results/topo.txt`: topology snapshot used for placement labeling
- `SYSV/results/degradation_warnings.log`: deduplicated run warnings
- `SYSV/results/perf_stat.csv`: perf stat counters (if profiled)
- `SYSV/results/stats_report.txt`: textual statistical summary
- `SYSV/results/tables/*.tex`: LaTeX tables for paper inclusion
- `SYSV/results/figures/*.(pdf|png)`: generated publication figures

## Data Schema (all_runs.csv)

Primary columns emitted by benchmark binaries:

- `mechanism` (typically `mq_sysv` or `shm_sysv`)
- `benchmark`
- `n_procs`
- `msg_size_bytes`
- `placement`
- `run_id`
- `iterations`
- `avg_ns, p50_ns, p95_ns, p99_ns, p999_ns, min_ns, max_ns`
- `throughput_msg_s, throughput_MB_s`
- `mem_delta_kb`

## Requirements

- Linux with System V IPC enabled (`CONFIG_SYSVIPC`)
- GCC + Make
- Python 3 with: `numpy`, `scipy`, `matplotlib`, `pandas`
- `perf` tool for hardware counter profiling
- Sudo access for sysctl/perf settings

## Notes On Kernel Limits

System V queue depth and message size depend on:

- `kernel.msgmax`
- `kernel.msgmnb`
- `kernel.msgmni`

The provided Make targets configure these values before full sweeps.

## Repro Tips

- Use `make clean` to remove binaries and stale System V queues.
- Use `make distclean` to also remove generated results.
- Prefer running the full pipeline on an otherwise idle machine.
- Keep CPU topology and governor settings consistent across runs.
