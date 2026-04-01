# IPC Benchmark Suite — System V Message Queues

Measures kernel-level IPC performance using **System V message queues exclusively**.
Every send and receive goes through the kernel via `msgsnd` / `msgrcv`.
No shared memory. No semaphores. No pipes.

## Quick Start

Run the complete test suite and performance profiling with the following `make` targets:

```bash
make all              # compile all three binaries
make test             # smoke test (small iteration counts)
make run_all          # full sweep → results/results.csv
make run_perf_stat    # full sweep with CPU hardware counters (perf stat)
make run_perf_record  # full sweep with performance sampling (perf record)
---

## Kernel Requirement

System V message queues require `CONFIG_SYSVIPC=y` in the kernel.
This is enabled by default on all mainstream distributions (Ubuntu, Fedora,
Debian, RHEL, Arch). Verify with:

```bash
grep CONFIG_SYSVIPC /boot/config-$(uname -r)
# should print: CONFIG_SYSVIPC=y
```

Also verify the IPC namespace is accessible:

```bash
ipcs -q   # lists existing System V message queues (empty is fine)
```

If your kernel limits on message or queue size are too small, raise them before running:

```bash
# Check current limits
cat /proc/sys/kernel/msgmax    # max bytes per single message (default 8192)
cat /proc/sys/kernel/msgmnb    # max bytes per queue (default 16384)
cat /proc/sys/kernel/msgmni    # max number of queues system-wide (default 32000)

# Raise for large-message or deep-queue tests
sudo sysctl kernel.msgmax=65536
sudo sysctl kernel.msgmnb=2097152   # 2 MB per queue
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
│   ├── timing.h    — now_ns(), result_t, compute_stats(), CSV output (prints mq_sysv)
│   ├── affinity.h  — CPU pinning via sched_setaffinity  [unchanged from POSIX]
│   └── stats.h     — alloc_samples()                    [unchanged from POSIX]
├── mq_sysv/
│   ├── pingpong.c    — Family A: latency
│   ├── throughput.c  — Family B: throughput
│   └── scalability.c — Family C: contention
├── results/        — CSV output lands here
└── Makefile
```

> `affinity.h` and `stats.h` are identical to the POSIX version — they have
> no MQ dependencies. Only `timing.h` differs: `print_csv_row()` emits
> `mq_sysv` in the mechanism column so POSIX and System V results can be
> compared in the same CSV file.

---

## System V vs POSIX — Conceptual Differences

| Concept | POSIX MQ | System V MQ |
|---|---|---|
| Queue identity | Named path (`/ipc_pp_fwd`) | Integer `msqid` from `msgget(key, ...)` |
| Create / open | `mq_open(name, O_CREAT\|O_WRONLY, ...)` | `msgget(key, IPC_CREAT\|0600)` |
| Send | `mq_send(mqd, buf, sz, prio)` | `msgsnd(msqid, &msg, sz, 0)` |
| Receive | `mq_receive(mqd, buf, sz, NULL)` | `msgrcv(msqid, &msg, sz, mtype, 0)` |
| Message format | Raw buffer | `struct { long mtype; char mtext[]; }` |
| Queue depth | `mq_attr.mq_maxmsg` (message count) | `msg_qbytes` (total bytes) via `msgctl IPC_SET` |
| Separate R/W ends | Yes — distinct `mqd_t` per direction | No — all processes share one `msqid` |
| Destroy | `mq_unlink(name)` | `msgctl(msqid, IPC_RMID, NULL)` |
| Link flag | `-lrt` required | None — plain syscalls |

---

## Benchmarks

### Family A — Ping-Pong Latency (`bin/pingpong`)

Measures one-way latency of the System V MQ kernel path.

Two processes communicate over two queues (one in each direction):

```
P1 ──q_fwd──▶ P2
P1 ◀──q_bwd── P2
```

P1 sends a message and blocks on `msgrcv`. P2 wakes up, immediately
calls `msgsnd` back, and blocks again. P1 records the round-trip time
(RTT). One-way latency = RTT / 2.

Both `msgsnd` and `msgrcv` are blocking calls — each one triggers a
context switch through the kernel, so this benchmark captures the full
kernel scheduling cost of a message-passing round trip.

Queue capacity is set to one message (`msg_qbytes = sizeof(long) + msz`)
so the producer cannot send ahead; every iteration is a genuine blocking
round trip.

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
Producer ──msqid──▶ Consumer
```

The producer calls `msgsnd` in a tight loop for N messages.
The consumer calls `msgrcv` in a tight loop until all N arrive.

Queue capacity is set to `depth × (sizeof(long) + msz)` bytes via
`msgctl IPC_SET` on `msg_qbytes`. A deeper queue lets the producer run
ahead of the consumer without blocking, revealing the maximum bandwidth
the kernel MQ path can sustain. Throughput is measured as wall-clock
time around the producer's send loop.

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
| `-q` | Queue depth (in messages) | 64 |
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
P2 ─┤──▶ [shared msqid] ──▶ Consumer
P3 ─┘
```

Inside the kernel, every `msgsnd` acquires a lock on the queue
structure. As N increases, producers contend on that lock and
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
P1 ↔ P2      (own msqid pair)
P3 ↔ P4      (own msqid pair)
P5 ↔ P6      (own msqid pair)
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

To compare directly with POSIX results, append both suites into one file:

```bash
./bin/pingpong -H > combined.csv                          # header once
../posix_message_queues/bin/pingpong  ... >> combined.csv # mechanism=mq_posix
./bin/pingpong ...                        >> combined.csv # mechanism=mq_sysv
```

**CSV columns:**

| Column | Description |
|--------|-------------|
| `mechanism` | Always `mq_sysv` |
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
sorts the samples and fills avg, p50, p99, min, max, and throughput.
`print_csv_row()` formats one CSV line, with `mq_sysv` in the mechanism
column.

### `common/affinity.h`

Wraps `sched_setaffinity()` to pin the calling process to a specific
logical CPU. Without pinning, the OS can migrate a process mid-run and
cause a latency spike that pollutes percentiles. Non-fatal if pinning
fails (e.g. inside a restricted container).

### `pingpong.c`

Creates two System V queues via `msgget(KEY_FWD, IPC_CREAT|0600)` and
`msgget(KEY_BWD, IPC_CREAT|0600)` before `fork()`. Both processes use
the same integer `msqid` — System V has no separate read/write
descriptors. Each message is wrapped in `sv_msg_t { long mtype; char
mtext[]; }` with `mtype=1`. Queue capacity is set to one message via
`msgctl IPC_SET` on `msg_qbytes`, forcing every iteration to be a true
blocking round trip. Cleanup uses `msgctl(id, IPC_RMID, NULL)`.

### `throughput.c`

Creates one queue with `msg_qbytes = depth × (sizeof(long) + msz)` so
the producer can stay ahead of the consumer without blocking. The
producer loops calling `msgsnd`; the consumer loops calling `msgrcv`.
Elapsed time is measured around the producer's send loop only. A
`volatile` sink variable prevents the compiler from eliminating the
consumer's buffer read, keeping the benchmark realistic.

### `scalability.c`

**C1:** One queue is shared by all N producers. Each `msgsnd` acquires
the kernel's internal queue lock. As N increases, lock contention grows
and throughput drops. The test quantifies how steeply.

**C2:** Each pair gets its own two queues, identified by keys derived
from `KEY_C2_FWD + pair_index` and `KEY_C2_BWD + pair_index`. All
pairs run in parallel. Since no queue is shared, there is no lock
contention between pairs, but the scheduler must context-switch between
more processes and the kernel must manage more queue structures, which
affects latency. Each initiator child sends its per-iteration samples
back to the parent via a pipe; the parent aggregates them and computes
percentiles across all pairs.

---

## Inspecting Live Queues

System V queues persist until explicitly removed (unlike POSIX named
queues which can be unlinked). Use these commands to inspect or clean
up queues left behind by a crashed run:

```bash
# List all System V message queues
ipcs -q

# Remove a specific queue by msqid
ipcrm -q <msqid>

# Remove all System V message queues owned by current user
ipcs -q | awk 'NR>2 {print $2}' | xargs -r ipcrm -q
```

---

## Dependencies

```bash
# Build
sudo apt-get install gcc make

# Runtime — verify System V IPC is compiled in
grep CONFIG_SYSVIPC /boot/config-$(uname -r)

# Inspect / clean up queues
ipcs -q
ipcrm -q <msqid>

# Optional: hardware performance counters
sudo apt-get install linux-tools-generic
perf stat -e cycles,instructions,context-switches,cache-misses \
    ./bin/pingpong -n 5000 -s 64 -c 0,1
```
