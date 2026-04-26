# SYSV Stack in new_ieee_combined_tests

This folder contains the System V IPC benchmark implementations and build targets used by the top-level suite runner.

Important: this stack folder only contains code and binaries. Full orchestration, result collection, and analysis are handled from the repository root by scripts in scripts/.

## How this stack is used

Typical flow from repository root:

1. Build this stack: make -C stacks/SYSV all
2. Run full suite: bash scripts/run_suite.sh
3. Analyze merged results: python3 scripts/analyze.py

Inside this folder, make test is only a smoke test.

## Actual folder structure

SYSV/
  Makefile
  README.md
  bin/
  common/
    affinity.h
    stats.h
    timing.h
  mq_sysv/
    pingpong.c
    throughput.c
    scalability.c
  shm_sysv/
    shm_pingpong.c
  priority/
    priority_test.c

## What each test is doing

The benchmark binaries all emit rows with a shared CSV schema (benchmark name, process count, message size, placement label, latency percentiles, throughput, mem_delta_kb, run_id).

### 1) mq_sysv/pingpong.c (bin/pingpong)

- Pattern: 2 processes exchange one message forward and one reply back per iteration using two System V queues.
- Queue API: msgget/msgsnd/msgrcv/msgctl with integer queue IDs (not named paths).
- Measured quantity: round-trip time per iteration, then converted to one-way latency as RTT/2.
- Throughput interpretation: counts both transfer directions, so effective bytes per round are 2 * message_size.
- Why this exists: System V queue latency baseline directly comparable with POSIX mq pingpong.

### 2) mq_sysv/throughput.c (bin/throughput)

- Pattern: 1 producer sends N messages, 1 consumer drains them.
- Queue capacity model: queue byte capacity is controlled through msg_qbytes.
- Depth emulation: requested depth q is translated to bytes as q * (sizeof(long) + msg_size).
- Measured quantity: producer-side elapsed time around send loop, reported as msgs/s and MB/s.
- Why this exists: steady-state System V queue bandwidth test under configurable buffering.

### 3) mq_sysv/scalability.c (bin/scalability)

Two modes are implemented:

- c1 fan-in mode:
  N producers -> 1 consumer on one shared queue.
  Measures throughput collapse/survival under lock contention.

- c2 parallel-pairs mode:
  N independent ping-pong pairs run concurrently (2 queues per pair).
  Measures one-way tail latency as concurrency increases.

Other details:

- Uses key-derived queue IDs per test/pair.
- Creates all required queues before forking worker processes.

### 4) shm_sysv/shm_pingpong.c (bin/shm_pingpong)

- Pattern: same ping-pong logic as queue pingpong, but payload lives in System V shared memory.
- Synchronization: two System V semaphores for forward/backward turn-taking.
- Measured quantity: one-way latency (RTT/2), same style as queue pingpong.
- Why this exists: copy-free baseline to isolate queue syscall/copy overhead.

### 5) priority/priority_test.c (bin/priority_test)

System V has no native message-priority field like POSIX MQ. This test emulates two-level priority with mtype and msgrcv selection rules.

Three sub-tests are implemented with a POSIX-compatible benchmark naming scheme:

- T1 head-of-line jump:
  Continuous LOW/HIGH producer stream with concurrent draining.
  Measures how far HIGH messages jump ahead of FIFO order.

- T2 backpressure divergence:
  Consumer is intentionally throttled so queue remains loaded.
  Measures HIGH vs LOW latency distributions under pressure.

- T3 multi-producer preservation:
  N producers start together (barrier) and send concurrently.
  Measures ordering preservation behavior and latency under true contention.

Why this exists: keep methodology comparable to POSIX priority tests while documenting System V behavior under the same workload pattern.

## Build and local smoke run in this folder

- Build all: make all
- Smoke test: make test
- Clean: make clean

The full multi-run benchmark matrix is driven by root scripts/run_suite.sh, not by this folder alone.
