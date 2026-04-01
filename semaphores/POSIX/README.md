# IPC Benchmark Suite — POSIX Semaphores

Measures kernel-level IPC performance across POSIX mechanisms:

| Suite | Mechanism | Kernel API |
|---|---|---|
| `sem_posix` | POSIX semaphores | `sem_post` / `sem_wait` |

The suite runs three benchmark families (latency, throughput, scalability) and writes rows into a single CSV. No shared memory. No pipes in the IPC hot path.

---

## Kernel Requirements

### POSIX Semaphores

Named semaphores (`sem_open`) are available on all Linux kernels with glibc.
No special kernel config is needed. Semaphore objects appear under `/dev/shm/sem.*`.

---

## Quick Start

```bash
make all        # build all 3 binaries
make test       # smoke test
make run_all    # full parameter sweep → results/results.csv
```

To build or run one suite at a time:

```bash
make sem        # build sem_posix only
make run_sem    # sweep sem_posix → results/results.csv
```

---

## Project Layout

```
ipc-bench/
├── common/
│   ├── timing.h      — now_ns(), result_t, compute_stats(), CSV output
│   │                   MECHANISM macro selects the mechanism string per suite
│   ├── affinity.h    — CPU pinning via sched_setaffinity
│   └── stats.h       — alloc_samples()
├── sem_posix/
│   ├── pingpong.c    — Family A: latency
│   ├── throughput.c  — Family B: throughput
│   └── scalability.c — Family C: contention
├── results/          — CSV output lands here
└── Makefile          — builds suite; sweep is fully inlined (no shell script)
```

Binaries are placed under:
```
bin/sem_posix/{pingpong,throughput,scalability}
```

---

## Benchmarks

All three families exist in the suite. The semaphore suite carries no payload (`msg_size=0`,
`throughput_MB_s=0`) because semaphores are pure signalling primitives.

---

### Family A — Ping-Pong Latency

Measures one-way round-trip latency through the kernel IPC path.

```
P1 ──[fwd]──▶ P2
P1 ◀──[bwd]── P2
```

P1 sends (or posts) and blocks waiting for the reply. P2 wakes,
immediately echoes back, and blocks again. RTT is recorded per
iteration; one-way latency = RTT / 2. Channels are opened
before `fork()` so no startup race inflates the first sample.

**sem_posix** uses two named semaphores both initialised to 0.
P1 posts `fwd` and waits on `bwd`; P2 does the inverse.

```bash
# sem_posix — semaphores carry no payload, so -s is not applicable
bin/sem_posix/pingpong -n 10000 -c 0,1 -p diff_core

# Print CSV header
bin/sem_posix/pingpong -H
```

| Flag | sem_posix | Default |
|------|-----------|---------|
| `-n` | Iterations | 10000 |
| `-c` | CPU pin `P1,P2` | no pin |
| `-p` | Placement label | unspecified |
| `-H` | Print header and exit | — |

---

### Family B — Throughput

Measures peak operation rate from one producer to one consumer.

```
Producer ──[channel]──▶ Consumer
```

The producer runs in a tight loop for N iterations. A depth > 1
(`-q` flag) lets the producer run ahead without blocking, which
reveals the maximum sustained rate the kernel path can deliver.
Elapsed time is measured around the producer loop only.

**sem_posix** uses two semaphores (`sem_slots` init=depth, `sem_items`
init=0) — the classic bounded producer-consumer. `throughput_MB_s`
is always 0 because no data is transferred; `throughput_msg_s` reports
synchronisation operations per second.

```bash
# sem_posix — 100 000 ops, channel depth 64
bin/sem_posix/throughput -n 100000 -q 64 -p diff_core
```

| Flag | sem_posix | Default |
|------|-----------|---------|
| `-n` | Total ops | 100000 |
| `-q` | Semaphore channel depth | 64 |
| `-c` | CPU pin `producer,consumer` | no pin |
| `-p` | Placement label | unspecified |
| `-H` | Print header and exit | — |

---

### Family C — Scalability Under Contention

Two modes selected with `-m`:

#### C1 — Fan-in: N producers → 1 consumer

All N producers write to the same shared channel. The consumer drains it.

```
P0 ─┐
P1 ─┤──▶ [shared channel] ──▶ Consumer
P2 ─┘
```

**sem_posix:** every `sem_post` acquires the kernel's internal
semaphore lock.

```bash
# sem_posix — 4 producers, 5 000 posts each (no -s)
bin/sem_posix/scalability -m c1 -n 4 -k 5000
```

#### C2 — Parallel pairs: N independent ping-pong pairs

Each pair has its own private channel(s). All N pairs run concurrently.
No channel is shared between pairs.

```
I0 ↔ R0    (own channel pair)
I1 ↔ R1    (own channel pair)
I2 ↔ R2    (own channel pair)
```

Shows how per-pair latency degrades as N grows due to scheduler
pressure and increased kernel resource management, even without
contention on a shared channel.

Latency percentiles are aggregated across all pairs. Throughput
fields are zeroed — elapsed covers pipe-drain time for result
collection, not IPC time.

```bash
# sem_posix
bin/sem_posix/scalability -m c2 -n 4 -k 2000
```

| Flag | sem_posix | Default |
|------|-----------|---------|
| `-m` | `c1` or `c2` (required) | — |
| `-n` | Producers (c1) or pairs (c2) | 4 |
| `-k` | Ops per producer / iters per pair | 5000 |
| `-p` | Placement label | unspecified |
| `-H` | Print header and exit | — |

---

## Output Format

Every binary writes one CSV row to stdout. All binaries share the
same column layout — the `mechanism` field distinguishes the suite.

```
mechanism,benchmark,n_procs,msg_size_bytes,placement,iterations,
avg_ns,p50_ns,p99_ns,min_ns,max_ns,throughput_msg_s,throughput_MB_s
```

| Column | Description |
|--------|-------------|
| `mechanism` | `sem_posix` |
| `benchmark` | `pingpong`, `throughput`, `fanin_nN`, `pairs_nN` |
| `n_procs` | Total process count |
| `msg_size_bytes` | Payload size (0 for sem_posix) |
| `placement` | Label you pass with `-p` |
| `iterations` | Operations measured |
| `avg_ns` | Mean latency (ns) |
| `p50_ns` | Median latency |
| `p99_ns` | 99th-percentile latency |
| `min_ns` / `max_ns` | Extremes |
| `throughput_msg_s` | Ops per second |
| `throughput_MB_s` | Effective bandwidth (0 for sem_posix) |

Accumulate runs manually:

```bash
bin/sem_posix/pingpong -H > results/results.csv
bin/sem_posix/pingpong -n 10000       -p same_core >> results/results.csv
```

Or let `make run_all` do the full sweep automatically.

---

## How the Code Works

### `common/timing.h`

`now_ns()` calls `clock_gettime(CLOCK_MONOTONIC_RAW)` — a kernel clock
not adjusted by NTP, with nanosecond resolution. `result_t` holds the
sample array and all computed statistics. `compute_stats()` sorts the
samples (making a copy) and fills avg, p50, p99, min, max, and
throughput. `print_csv_row()` formats one CSV line.

The `MECHANISM` preprocessor macro controls the first CSV field.
It is injected at compile time by the Makefile:

```
sem_posix/ →  -DMECHANISM='"sem_posix"'
```

### `common/affinity.h`

Wraps `sched_setaffinity()` to pin the calling process to a specific
logical CPU.

### `sem_posix/pingpong.c`

Two named semaphores, both initialised to 0.

### `sem_posix/throughput.c`

Two semaphores: `sem_slots` (init=depth) and `sem_items` (init=0).
Producer: `sem_wait(slots)` + `sem_post(items)`. Consumer: inverse.
This is the textbook pure-semaphore bounded producer-consumer.

### `sem_posix/scalability.c`

**C1:** One shared channel, N producers, one consumer. Contention is
on the kernel's internal lock (semaphore lock). Reports throughput as N grows.

**C2:** N independent ping-pong pairs, all forked and running
concurrently. Each pair uses its own private channel(s). Initiator
children send latency samples back to the parent via pipes (one pipe
per pair) — pipes are used only for result collection, never in the
IPC hot path. Throughput fields are zeroed for C2 since elapsed
covers pipe-drain time; only latency percentiles are meaningful.

---

## Dependencies

```bash
# Build tools
sudo apt-get install gcc make

# Optional: hardware performance counters
sudo apt-get install linux-tools-generic
perf stat -e cycles,instructions,context-switches,cache-misses \
    bin/sem_posix/pingpong -n 5000       -c 0,1
```
