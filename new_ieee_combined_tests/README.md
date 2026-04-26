# POSIX vs System V IPC — Benchmark Suite

A self-contained benchmark harness that compares **POSIX** and **System V** inter-process communication (message queues and shared memory) across latency, throughput, and scalability dimensions.

CPU placement uses two topology-aware modes:

- **`same_core`** — two logical CPUs on one physical core (SMT siblings)
- **`cross_core`** — two CPUs on different physical cores (same socket preferred)

No NUMA / cross-socket placement is used.

---

## Benchmarks (5 tests)

| # | Test | Binary | What it measures |
|---|------|--------|-----------------|
| 1 | MQ ping-pong | `pingpong` | Round-trip latency of message queue send/receive pairs |
| 2 | MQ throughput | `throughput` | Sustained message rate at varying queue depths |
| 3 | MQ fan-in | `scalability -m c1` | Many-to-one producer convergence |
| 4 | MQ parallel pairs | `scalability -m c2` | Independent sender/receiver pair scaling |
| 5 | SHM ping-pong | `shm_pingpong` | Shared-memory baseline latency for comparison |

---

## Project Structure

```text
new_ieee_combined_tests/
├── config/
│   └── defaults.env                  # Run counts, sizes, depths, and quick-mode overrides
├── stacks/
│   ├── POSIX/                        # POSIX IPC source stack (builds to stacks/POSIX/bin/)
│   └── SYSV/                         # SysV IPC source stack  (builds to stacks/SYSV/bin/)
├── scripts/
│   ├── detect_cpu_pairs.sh           # Detects same_core / cross_core CPU pairs
│   ├── setup_sysctl.sh               # Applies kernel IPC sysctl limits
│   ├── setup_perf.sh                 # Sets perf_event_paranoid / kptr_restrict
│   ├── run_suite.sh                  # Main benchmark runner (raw CSV output)
│   ├── run_perf.sh                   # Perf-stat hardware counter collection
│   ├── run_strace.sh                 # Strace syscall summary collection
│   ├── combine_results.sh            # Merges POSIX + SysV raw CSVs
│   ├── analyze.py                    # Analysis: tables, figures, report
│   └── analyze_gpt.py                # Alternative GPT-assisted analysis
├── results/
│   ├── raw/                          # run_suite.sh output
│   │   ├── posix/all_runs.csv
│   │   ├── sysv/all_runs.csv
│   │   └── combined/all_runs.csv
│   ├── perf/                         # run_perf.sh output
│   │   ├── posix/all_perf.csv
│   │   └── sysv/all_perf.csv
│   ├── strace/                       # run_strace.sh output
│   │   ├── posix/strace_summary.txt
│   │   └── sysv/strace_summary.txt
│   ├── analysis/                     # analyze.py output
│   │   ├── figures/
│   │   ├── tables/
│   │   └── analysis_report.md
│   └── logs/
│       ├── topology.txt
│       ├── posix_run.log
│       ├── sysv_run.log
│       ├── posix_perf.log
│       ├── sysv_perf.log
│       ├── posix_strace.log
│       └── sysv_strace.log
├── Makefile
└── README.md
```

---

## Prerequisites

- **Build tools:** `make`, `gcc`, `bash`
- **Analysis:** `python3`, `pandas`, `matplotlib`
- **Profiling (optional):** `perf` (linux-tools), `strace`

Install analysis dependencies:

```bash
python3 -m pip install pandas matplotlib
```

---

## Analysis Pipeline

The project follows a **three-phase** approach — each phase answers different questions about IPC performance:

### Phase 1: Benchmark Suite (`run_suite.sh`) — *"How fast is it?"*

The main benchmark runner collects **application-level timing data**: latency (ns/roundtrip), throughput (msg/s), and scalability metrics across all message sizes, queue depths, and producer/pair counts defined in `defaults.env`.

This gives you the headline numbers — which IPC mechanism is faster, by how much, and under what conditions.

```bash
bash scripts/run_suite.sh                # Run full matrix for both stacks
python3 scripts/analyze.py               # Generate tables, figures, and report
```

**Output:** `results/raw/{posix,sysv}/all_runs.csv` → merged into `results/raw/combined/all_runs.csv`

### Phase 2: Perf Profiling (`run_perf.sh`) — *"Why is it fast (or slow)?"*

Once you have the timing data, `perf stat` reveals the **hardware-level story** behind the numbers. It uses CPU performance counters (PMU hardware registers) to measure what the processor is actually doing during each benchmark run.

**What `perf stat` collects:**

| Counter | What it tells you |
|---------|-------------------|
| `task-clock` | Total CPU time consumed (ms) |
| `cycles` | CPU clock cycles spent — raw measure of work |
| `instructions` | Instructions executed; compare with cycles to get IPC (instructions per cycle) |
| `cache-references` | L3/LLC cache lookups — indicates data access patterns |
| `cache-misses` | Cache lookups that went to main memory — high values indicate poor data locality |
| `minor-faults` | Page faults resolved from page cache (no disk I/O) |
| `major-faults` | Page faults requiring disk reads (should be near zero for IPC) |
| `context-switches` | Voluntary + involuntary context switches — measures kernel scheduling overhead |
| `cpu-migrations` | Times the process was moved between CPUs — indicates scheduler interference |

**Why this matters:** If POSIX MQ shows higher `cache-misses` than SysV MQ at the same message size, that explains a latency gap more precisely than timing alone. Similarly, `context-switches` directly reveals kernel-crossing overhead differences between the two IPC stacks.

```bash
bash scripts/run_perf.sh                 # Runs same matrix as run_suite.sh, wrapped in perf stat
```

**Output:** `results/perf/{posix,sysv}/all_perf.csv` — one row per (benchmark, config, run, event) with full metadata columns.

### Phase 3: Strace Profiling (`run_strace.sh`) — *"What system calls does it make?"*

While `perf` shows hardware behaviour, `strace` shows **kernel-level behaviour** — the exact system calls each IPC mechanism uses and how much time is spent in each.

**What `strace -c` collects:**

For each benchmark run, strace intercepts every system call and produces a summary table showing:
- **calls** — how many times each syscall was invoked
- **time (%)** — fraction of total time spent in each syscall  
- **usecs/call** — average microseconds per invocation
- **errors** — failed syscall count

**Why this matters:** POSIX MQ uses `mq_open`, `mq_send`, `mq_receive`, `mq_close`, `mq_unlink`. SysV MQ uses `msgget`, `msgsnd`, `msgrcv`, `msgctl`. Shared memory uses `shm_open`/`shmget` + `mmap`. The strace output lets you directly compare:
- How many kernel transitions each mechanism requires per message
- Which syscalls dominate wall-clock time
- Whether one stack has unexpected overhead (e.g. extra `futex` or `clone` calls)

To keep output readable, `run_strace.sh` traces only a **representative subset** of configurations (8 configs at 64B) rather than the full matrix.

```bash
bash scripts/run_strace.sh               # Traces representative configs only
```

**Output:** `results/strace/{posix,sysv}/strace_summary.txt` — human-readable, clearly sectioned.

### Complete Workflow

```bash
# Cache sudo credentials (one time)
sudo -v                        
# Phase 1: Timing data
bash scripts/run_suite.sh
python3 scripts/analyze.py

# Phase 2: Hardware counters (deeper analysis)
bash scripts/run_perf.sh

# Phase 3: Syscall profiling (deeper analysis)
bash scripts/run_strace.sh
```

Or use Makefile shortcuts:

```bash
make run          # Phase 1: Full benchmark suite (both stacks)
make quick        # Phase 1: Smoke test (reduced iterations)
make analyze      # Phase 1: Generate analysis report
make perf         # Phase 2: Perf-stat hardware counters
make strace       # Phase 3: Strace syscall summaries
make all          # Phase 1: run + analyze
make clean        # Remove all generated outputs
```

> **Note:** Each script is self-contained — it handles kernel sysctl setup, ulimit raising, and CPU governor configuration internally. You do not need to run `run_suite.sh` before `run_perf.sh` or `run_strace.sh`, but the intended workflow is to start with the timing data and then dig deeper.

---

## Scripts Reference

### `run_suite.sh` — Main Benchmark Runner

Builds binaries, applies system setup, and runs the full test matrix for POSIX and/or SysV stacks.

**Test matrix:**

| Parameter | Values (from `defaults.env`) |
|-----------|------------------------------|
| Ping-pong sizes | `PINGPONG_SIZES` (default: 8 64 256 1024 4096 8192) |
| Throughput sizes | `THROUGHPUT_SIZES` × `THROUGHPUT_DEPTHS` |
| Scalability sizes | `SCALABILITY_SIZES` × `FANIN_N` / `PAIRS_N` |
| Placements | `same_core`, `cross_core` |
| Runs per config | `RUNS` (default: 1) |

**Output:** `results/raw/{posix,sysv}/all_runs.csv`

**Options:**

```
--only {both|posix|sysv}   Stack selection (default: both)
--runs N                   Override repetition count
--quick                    Smoke mode (small iteration counts)
--no-build                 Skip make
--skip-setup-sysctl        Skip kernel sysctl setup
--setup-perf               Also apply perf sysctl setup
```

---

### `run_perf.sh` — Hardware Counter Collection

Wraps every benchmark invocation with `perf stat` to collect hardware performance counters. Runs the **same full test matrix** as `run_suite.sh`.

**Perf events collected:**

`task-clock`, `cycles`, `instructions`, `cache-references`, `cache-misses`, `minor-faults`, `major-faults`, `context-switches`, `cpu-migrations`

**Output:** Single CSV per stack:

- `results/perf/posix/all_perf.csv`
- `results/perf/sysv/all_perf.csv`

**CSV columns:**

```
mechanism,benchmark,placement,msg_size,queue_depth,n_procs,run_id,event,value
```

Example rows:

```csv
posix,pingpong,same_core,64,,,1,cycles,4523890
posix,throughput,cross_core,1024,32,,2,cache-misses,789
posix,scalability_fanin,,256,,8,1,instructions,999999
```

**Options:**

```
--stack {both|posix|sysv}  Stack selection (default: both)
--runs N                   Override PERF_RUNS (default: 3, recommended: 3–5)
--quick                    Smoke mode
--no-build                 Skip make
--skip-setup-sysctl        Skip kernel sysctl setup
--skip-setup-perf          Skip perf sysctl setup
```

**System setup:** Automatically applies sysctl limits, perf access (`perf_event_paranoid=-1`), raises ulimits, and sets CPU governor to `performance`.

---

### `run_strace.sh` — Syscall Profiling

Collects `strace -c` syscall summaries for a **reduced set of representative configurations** (not the full matrix). Designed for readable, analysis-friendly output.

**Configurations traced (8 total):**

| Benchmark | Placement | Detail |
|-----------|-----------|--------|
| MQ pingpong | same_core | 64B |
| MQ pingpong | cross_core | 64B |
| SHM pingpong | same_core | 64B |
| SHM pingpong | cross_core | 64B |
| MQ throughput | same_core | depth=1, 64B |
| MQ throughput | same_core | depth=128, 64B |
| MQ fan-in | — | n=8, 64B |
| MQ pairs | — | n=4, 64B |

**Output:** Single summary file per stack:

- `results/strace/posix/strace_summary.txt`
- `results/strace/sysv/strace_summary.txt`

Each benchmark section is clearly delimited with headers showing the benchmark name, placement, and configuration.

**Options:**

```
--stack {both|posix|sysv}  Stack selection (default: both)
--runs N                   Override STRACE_RUNS (default: 1)
--quick                    Smoke mode
--no-build                 Skip make
--skip-setup-sysctl        Skip kernel sysctl setup
```

**System setup:** Same as `run_suite.sh` (sysctl, ulimits, CPU governor).

---

### Other Scripts

| Script | Purpose |
|--------|---------|
| `detect_cpu_pairs.sh` | Detects and exports `SAME_CORE_CPUS` and `CROSS_CORE_CPUS` pairs from `/sys` topology. Use `--export` (default) or `--summary`. |
| `setup_sysctl.sh` | Applies kernel IPC limits (message queue sizes, shared memory) via `sysctl`. Requires `sudo`. |
| `setup_perf.sh` | Sets `kernel.perf_event_paranoid=-1` and `kernel.kptr_restrict=0` for unprivileged perf access. Requires `sudo`. |
| `combine_results.sh` | Merges `results/raw/posix/all_runs.csv` + `results/raw/sysv/all_runs.csv` into `results/raw/combined/all_runs.csv`. |
| `analyze.py` | Reads the combined CSV and generates summary tables, comparison figures, and `analysis_report.md`. |

---

## Configuration Reference (`config/defaults.env`)

| Variable | Default | Description |
|----------|---------|-------------|
| `RUNS` | 1 | Repetitions per configuration (main suite) |
| `PERF_RUNS` | 3 | Repetitions per configuration (perf collection) |
| `STRACE_RUNS` | 1 | Repetitions per configuration (strace collection) |
| `PP_ITERS` | 50000 | Ping-pong iterations per run |
| `TP_MSGS` | 200000 | Throughput messages per run |
| `SC_K` | 10000 | Messages per producer/pair for scalability |
| `PINGPONG_SIZES` | 8 64 256 1024 4096 8192 | Payload sizes for ping-pong tests |
| `THROUGHPUT_SIZES` | 8 64 256 1024 4096 8192 | Payload sizes for throughput tests |
| `THROUGHPUT_DEPTHS` | 1 8 32 128 512 | Queue depths for throughput tests |
| `SCALABILITY_SIZES` | 64 256 1024 | Payload sizes for scalability tests |
| `FANIN_N` | 1 2 4 8 16 | Producer counts for fan-in (`-m c1`) |
| `PAIRS_N` | 1 2 4 8 | Pair counts for parallel pairs (`-m c2`) |
| `QUICK_*` | (reduced) | Quick-mode overrides for smoke testing |

---

## Output Summary

| Output | Source | Description |
|--------|--------|-------------|
| `results/raw/posix/all_runs.csv` | `run_suite.sh` | Raw POSIX benchmark data |
| `results/raw/sysv/all_runs.csv` | `run_suite.sh` | Raw SysV benchmark data |
| `results/raw/combined/all_runs.csv` | `combine_results.sh` | Merged POSIX + SysV data |
| `results/perf/posix/all_perf.csv` | `run_perf.sh` | POSIX hardware counter data |
| `results/perf/sysv/all_perf.csv` | `run_perf.sh` | SysV hardware counter data |
| `results/strace/posix/strace_summary.txt` | `run_strace.sh` | POSIX syscall summary |
| `results/strace/sysv/strace_summary.txt` | `run_strace.sh` | SysV syscall summary |
| `results/analysis/tables/*.csv` | `analyze.py` | Grouped statistics tables |
| `results/analysis/figures/*` | `analyze.py` | Comparison plots |
| `results/analysis/analysis_report.md` | `analyze.py` | Textual analysis report |
| `results/logs/topology.txt` | `run_suite.sh` | CPU topology + config snapshot |
| `results/logs/*_run.log` | `run_suite.sh` | Build + runtime stderr logs |
| `results/logs/*_perf.log` | `run_perf.sh` | Perf run build logs |
| `results/logs/*_strace.log` | `run_strace.sh` | Strace run build logs |

---

## Notes

- The `scalability` binary does not expose a CPU-pair option (`-c`), so CPU placement is only applied to ping-pong and throughput tests.
- CPU pair selection is logged in `results/logs/topology.txt`.
- Each profiling script (`run_perf.sh`, `run_strace.sh`) is fully self-contained — it handles sysctl setup, ulimits, and CPU governor internally. You do **not** need to run `run_suite.sh` first.
- `run_perf.sh` enables `perf_event_paranoid=-1` by default; use `--skip-setup-perf` to disable.
