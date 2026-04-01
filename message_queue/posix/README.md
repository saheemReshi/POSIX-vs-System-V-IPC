# IPC Benchmark Suite — POSIX Message Queues

Measures kernel-level IPC performance using **POSIX message queues exclusively**.
Every send and receive goes through the kernel via `mq_send` / `mq_receive`.
No shared memory. No semaphores. No pipes.

---

## Kernel Requirement

POSIX message queues require `CONFIG_POSIX_MQUEUE=y` in the kernel.
This is enabled by default on all mainstream distributions (Ubuntu, Fedora,
Debian, RHEL, Arch). Verify with:

```bash
grep CONFIG_POSIX_MQUEUE /boot/config-$(uname -r)
# should print: CONFIG_POSIX_MQUEUE=y
```

The mqueue filesystem must also be mounted (usually automatic, or do it manually):

```bash
mount -t mqueue none /dev/mqueue
```

If your kernel limit on message size is too small, raise it before running:

```bash
# Check current limits
cat /proc/sys/fs/mqueue/msg_max        # default 10
cat /proc/sys/fs/mqueue/msgsize_max    # default 8192

# Raise for large-message tests (4KB, 64KB)
sudo sysctl fs.mqueue.msg_max=1024
sudo sysctl fs.mqueue.msgsize_max=65536
```

---

## Quick Start

```bash
make all        # compile all three binaries
make test       # smoke test (small iteration counts)
make run_all    # full sweep → results/results.csv
```

---

## Project Layout

```
ipc-bench/
├── common/
│   ├── timing.h    — now_ns(), result_t, compute_stats(), CSV output
│   ├── affinity.h  — CPU pinning via sched_setaffinity
│   └── stats.h     — alloc_samples()
├── mq_posix/
│   ├── pingpong.c    — Family A: latency
│   ├── throughput.c  — Family B: throughput
│   └── scalability.c — Family C: contention
├── scripts/
│   └── run_all.sh  — full sweep script
├── results/        — CSV output lands here
└── Makefile
```

---

## Benchmarks

### Family A — Ping-Pong Latency (`bin/pingpong`)

Measures one-way latency of the POSIX MQ kernel path.

Two processes communicate over two queues (one in each direction):

```
P1 ──q_fwd──▶ P2
P1 ◀──q_bwd── P2
```

P1 sends a message and blocks on `mq_receive`. P2 wakes up via
`mq_receive`, immediately calls `mq_send` back, and blocks again.
P1 records the round-trip time (RTT). One-way latency = RTT / 2.

Both `mq_send` and `mq_receive` are blocking calls — each one triggers a
context switch through the kernel, so this benchmark captures the full
kernel scheduling cost of a message-passing round trip.

The forward queue is created with `mq_maxmsg=1` so the producer cannot
send ahead; every iteration is a genuine blocking round trip.

**Run a single test:**
```bash
# Defaults: 10 000 iterations, 64-byte messages
./bin/pingpong

# 5 000 iterations, 1 KB messages
./bin/pingpong -n 5000 -s 1024

# Pin P1 to cpu0, P2 to cpu1
./bin/pingpong -c 0,1 -p diff_core

# Print CSV header only
./bin/pingpong -H
```

| Flag | Meaning | Default |
|------|---------|---------|
| `-n` | Iterations | 10000 |
| `-s` | Message size (bytes) | 64 |
| `-c` | CPU pin for P1,P2 (e.g. `0,1`) | no pin |
| `-p` | Placement label in CSV | unspecified |
| `-H` | Print CSV header and exit | — |

---

### Family B — Throughput (`bin/throughput`)

Measures peak message rate from one producer to one consumer.

```
Producer ──q──▶ Consumer
```

The producer calls `mq_send` in a tight loop for N messages.
The consumer calls `mq_receive` in a tight loop until all N arrive.

A queue depth > 1 (`-q` flag, default 64) lets the producer run ahead
of the consumer without blocking, revealing the maximum bandwidth the
kernel MQ path can sustain. Throughput is measured as wall-clock time
from the first send to the last send, divided by message count.

**Run a single test:**
```bash
# Defaults: 100 000 messages, 64 bytes, queue depth 64
./bin/throughput

# 50 000 messages, 4 KB each
./bin/throughput -n 50000 -s 4096

# Shallow queue (depth 4) — producer blocks more often
./bin/throughput -q 4

# Pin producer to cpu0, consumer to cpu1
./bin/throughput -c 0,1 -p diff_core
```

| Flag | Meaning | Default |
|------|---------|---------|
| `-n` | Total messages | 100000 |
| `-s` | Message size (bytes) | 64 |
| `-q` | Queue depth (mq_maxmsg) | 64 |
| `-c` | CPU pin for producer,consumer | no pin |
| `-p` | Placement label | unspecified |
| `-H` | Print CSV header and exit | — |

---

### Family C — Scalability (`bin/scalability`)

Two modes selected with `-m`:

#### C1 — Fan-in: N producers → 1 consumer

All N producers send to the **same queue**. The consumer drains it.

```
P1 ─┐
P2 ─┤──▶ [shared queue] ──▶ Consumer
P3 ─┘
```

Inside the kernel, every `mq_send` acquires a spinlock on the queue
structure. As N increases, producers contend on that lock, and
throughput degrades. This test makes that degradation visible.

```bash
# 4 producers, 5 000 messages each
./bin/scalability -m c1 -n 4 -k 5000

# 8 producers
./bin/scalability -m c1 -n 8 -k 5000 -s 64
```

#### C2 — Parallel pairs: N independent ping-pong pairs

Each pair has its own two queues (fwd + bwd). All N pairs run at the
same time. No queue is shared between pairs.

```
P1 ↔ P2      (own queue pair)
P3 ↔ P4      (own queue pair)
P5 ↔ P6      (own queue pair)
```

Shows how latency per pair degrades as N grows — the scheduler has
more runnable processes, and the kernel must manage more queue
structures simultaneously.

```bash
# 2 concurrent pairs, 2 000 iterations each
./bin/scalability -m c2 -n 2 -k 2000

# 4 concurrent pairs
./bin/scalability -m c2 -n 4 -k 2000
```

| Flag | Meaning | Default |
|------|---------|---------|
| `-m` | Mode: `c1` (fan-in) or `c2` (pairs) | required |
| `-n` | Producers (c1) or pairs (c2) | 4 |
| `-k` | Messages per producer / iters per pair | 5000 |
| `-s` | Message size (bytes) | 64 |
| `-p` | Placement label | unspecified |
| `-H` | Print CSV header and exit | — |

---

## Output Format

Every binary writes one CSV row to stdout. Redirect into a file to accumulate runs:

```bash
./bin/pingpong -H > results.csv
./bin/pingpong -n 10000 -s 64  -p same_core >> results.csv
./bin/pingpong -n 10000 -s 64  -c 0,1 -p diff_core >> results.csv
./bin/throughput -n 100000 -s 64 >> results.csv
./bin/scalability -m c1 -n 4 -k 5000 >> results.csv
```

**CSV columns:**

| Column | Description |
|--------|-------------|
| `mechanism` | Always `mq_posix` |
| `benchmark` | `pingpong`, `throughput`, `fanin_nN`, `pairs_nN` |
| `n_procs` | Total process count |
| `msg_size_bytes` | Payload size |
| `placement` | Your label (`same_core`, `diff_core`, etc.) |
| `iterations` | Messages or ping-pong rounds measured |
| `avg_ns` | Mean latency (ns) |
| `p50_ns` | Median latency |
| `p99_ns` | 99th-percentile latency |
| `min_ns` / `max_ns` | Extremes |
| `throughput_msg_s` | Messages per second |
| `throughput_MB_s` | Effective bandwidth |

---

## How the Code Works

### `common/timing.h`

`now_ns()` calls `clock_gettime(CLOCK_MONOTONIC_RAW)` — a kernel clock
that is not adjusted by NTP and has nanosecond resolution. `result_t`
holds the sample array and all computed statistics. `compute_stats()`
sorts the samples (making a copy) and fills avg, p50, p99, min, max,
and throughput. `print_csv_row()` formats one line of output.

### `common/affinity.h`

Wraps `sched_setaffinity()` to pin the calling process to a specific
logical CPU. Without pinning, the OS can migrate a process mid-run and
cause a latency spike that pollutes percentiles. Non-fatal if pinning
fails (e.g. inside a restricted container).

### `pingpong.c`

Creates two named queues (`/ipc_bench_pp_fwd`, `/ipc_bench_pp_bwd`)
before `fork()`. Both processes open the queues after forking. P1 owns
the write end of `q_fwd` and the read end of `q_bwd`; P2 is the
reverse. Each `mq_send` / `mq_receive` pair crosses the kernel
scheduler — the latency measured is the full cost of that round trip.
Queue depth is fixed at 1 so the measurement is always a true blocking
send-and-wait, not a buffered send.

### `throughput.c`

Creates one queue with `mq_maxmsg=64`. The producer loops calling
`mq_send` without waiting; the consumer loops calling `mq_receive`.
The deeper queue lets the producer stay ahead of the consumer, keeping
the kernel pipeline full and measuring the saturation throughput of the
MQ path. Elapsed time is measured around the producer's send loop only.

### `scalability.c`

**C1:** One queue is shared by all producers. Each `mq_send` from a
producer acquires the kernel's internal queue lock. As N producers
increase, lock contention grows and throughput drops. The test
quantifies how steeply.

**C2:** Each pair gets its own two queues. All pairs run in parallel —
responders are forked, then the parent drives each initiator in turn.
Since no queue is shared, there is no lock contention between pairs,
but the scheduler must context-switch between more processes and the
kernel must manage more queue descriptors, which affects latency.

---

## Dependencies

```bash
# Build
sudo apt-get install gcc make

# Runtime — verify POSIX MQ is compiled in
grep CONFIG_POSIX_MQUEUE /boot/config-$(uname -r)

# Optional: hardware performance counters
sudo apt-get install linux-tools-generic
perf stat -e cycles,instructions,context-switches,cache-misses \
    ./bin/pingpong -n 5000 -s 64 -c 0,1
```
