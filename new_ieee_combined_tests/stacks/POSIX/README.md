# POSIX Stack in new_ieee_combined_tests

This folder contains the POSIX IPC benchmark implementations and build targets used by the top-level suite runner.

Important: this stack folder only contains code and binaries. Full orchestration, result collection, and analysis are handled from the repository root by scripts in scripts/.

## How this stack is used

Typical flow from repository root:

1. Build this stack: make -C stacks/POSIX all
2. Run full suite: bash scripts/run_suite.sh
3. Analyze merged results: python3 scripts/analyze.py

Inside this folder, make test is only a smoke test.

## Actual folder structure

POSIX/
  Makefile
  README.md
  bin/
  common/
    affinity.h
    stats.h
    timing.h
  mq_posix/
    pingpong.c
    throughput.c
    scalability.c
  shm_posix/
    shm_pingpong.c
  priority/
    priority_test.c

## What each test is doing

The benchmark binaries all emit rows with a shared CSV schema (benchmark name, process count, message size, placement label, latency percentiles, throughput, mem_delta_kb, run_id).

### 1) mq_posix/pingpong.c (bin/pingpong)

- Pattern: 2 processes exchange one message forward and one reply back per iteration using two POSIX queues.
- Queue setup: depth 1 by design, so each send blocks until the peer receives.
- Measured quantity: round-trip time per iteration, then converted to one-way latency as RTT/2.
- Throughput interpretation: counts both transfer directions, so effective bytes per round are 2 * message_size.
- Why this exists: baseline queue latency test under strict request/reply synchronization.

### 2) mq_posix/throughput.c (bin/throughput)

- Pattern: 1 producer sends N messages, 1 consumer drains them.
- Queue setup: requested depth q lets producer run ahead.
- Measured quantity: producer-side elapsed time around send loop, reported as msgs/s and MB/s.
- Robustness behavior: if requested queue depth is rejected by kernel/resource limits, depth is automatically halved until creation succeeds.
- Why this exists: steady-state queue bandwidth test, separate from ping-pong latency.

### 3) mq_posix/scalability.c (bin/scalability)

Two modes are implemented:

- c1 fan-in mode:
  N producers -> 1 consumer on one shared queue.
  Measures throughput collapse/survival under lock contention.

- c2 parallel-pairs mode:
  N independent ping-pong pairs run concurrently (2 queues per pair).
  Measures one-way tail latency as concurrency increases.

Other details:

- Queue descriptors are opened before fork to reduce startup races.
- c1 now also degrades depth under limits instead of hard-failing.

### 4) shm_posix/shm_pingpong.c (bin/shm_pingpong)

- Pattern: same ping-pong logic as queue pingpong, but payload lives in shared memory.
- Synchronization: two POSIX named semaphores (forward and backward turn-taking).
- Measured quantity: one-way latency (RTT/2), same style as queue pingpong.
- Why this exists: copy-free baseline to isolate queue syscall/copy overhead.

### 5) priority/priority_test.c (bin/priority_test)

Three sub-tests are implemented with POSIX MQ priorities:

- T1 head-of-line jump:
  Producer continuously alternates LOW/HIGH messages while consumer drains.
  Measures how far HIGH messages jump ahead of FIFO arrival order.

- T2 backpressure divergence:
  Consumer is intentionally throttled so queue remains loaded.
  Measures HIGH vs LOW latency distributions under pressure.

- T3 multi-producer preservation:
  N producers start together (barrier) and send concurrently.
  Measures priority preservation ratio and latency under true contention.

Why this exists: expose priority semantics and inversion behavior beyond simple latency/throughput tests.

## Build and local smoke run in this folder

- Build all: make all
- Smoke test: make test
- Clean: make clean

The full multi-run benchmark matrix is driven by root scripts/run_suite.sh, not by this folder alone.
