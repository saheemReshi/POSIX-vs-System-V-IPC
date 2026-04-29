# POSIX vs System V IPC — Changes, Limitations, and Future Scope

Generated: 2026-04-28

This document records every change made to the benchmark suite during the
review-and-revise round, the limitations the suite still has, and items we
did not address that should be acknowledged in the paper as either threats
to validity or future scope.

---

## 1. Changes made

### 1.1 New benchmarks — SHM coverage extended

Until this round, only `shm_pingpong` existed for shared memory. The paper
headline "POSIX vs SysV IPC" therefore had asymmetric coverage (MQ had
ping-pong + throughput + scalability; SHM had ping-pong only).

Added four new C sources, all mirroring the existing MQ designs so SHM and
MQ rows are directly comparable:

| File | Purpose |
|---|---|
| `stacks/POSIX/shm_posix/shm_throughput.c` | SPSC ring buffer over POSIX `shm_open` + named POSIX semaphores. |
| `stacks/POSIX/shm_posix/shm_pairs.c` | N parallel SHM ping-pong pairs over POSIX shm + named semaphores. |
| `stacks/SYSV/shm_sysv/shm_throughput.c` | SPSC ring buffer over SysV `shmget` + SysV `semop`. |
| `stacks/SYSV/shm_sysv/shm_pairs.c` | N parallel SHM ping-pong pairs over SysV shm + sem. |

Design notes:

- SHM throughput uses a **ring buffer of `depth` slots × `msg_size` bytes**.
  Two semaphores: `empty_slots` initialised to `depth`, `full_slots` to 0.
  Producer: `wait(empty); memcpy; post(full)`. Consumer: mirror.
  No explicit atomics needed — semaphores enforce capacity and ordering.
- SHM pairs creates `N` independent shm regions (each `2 * msg_size` bytes)
  with their own pair of semaphores. Each pair runs initiator + responder
  in its own pinned process. Per-iteration RTTs are returned to the parent
  over per-pair pipes. Aggregated p50/p95/p99/p99.9 reported.
- Both stacks use the same algorithm so any latency/throughput gap is
  attributable to the synchronisation primitive (POSIX named `sem_t` vs
  SysV `semop`).

### 1.2 Pre-existing bug fixes

- **SysV mechanism mislabelled.** `mq_sysv/pingpong.c`, `mq_sysv/throughput.c`,
  and both `scalability.c` codepaths were calling `print_csv_row()` from
  `timing.h`, which hardcodes `mechanism = "mq_posix"`. Every SysV row was
  therefore being emitted with the POSIX mechanism label and would have
  silently corrupted any analysis. All four call sites switched to
  `print_csv_row_mech("mq_sysv", ...)`.
- **SysV throughput silent capacity truncation.** `msgctl(IPC_SET)` for
  `msg_qbytes` was marked "(non-fatal)". When `kernel.msgmnb` was lower
  than the requested capacity, IPC_SET failed silently and the queue ran
  at the kernel default — making the `-q depth` parameter meaningless
  beyond the cap. Now hard-fails with a sysctl suggestion, **and**
  re-reads `msg_qbytes` after IPC_SET to verify the kernel applied the
  requested value (some kernels round).

### 1.3 Measurement-level fixes

- **Warmup loops** added to all timed binaries:
  `mq_posix/pingpong.c`, `mq_sysv/pingpong.c`,
  `mq_posix/throughput.c`, `mq_sysv/throughput.c`,
  `shm_posix/shm_pingpong.c`, `shm_sysv/shm_pingpong.c`,
  plus the four new SHM binaries. Warmup count is `min(5% × iters, 5000)`.
  Discards cold-cache, page-fault, and post-fork settle time.
- **Single-counted ping-pong throughput.** Both MQ and SHM ping-pong
  binaries previously reported `throughput_MB_s = (iters * 2 * msz) / sec`
  to count round-trip bytes. The throughput binary reports
  `(iters * msz) / sec`. The two metrics were not comparable. All
  ping-pongs now report single-counted throughput, matching the
  throughput binary's convention.
- **Memory-footprint measurement dropped.** `mem_delta_kb` is now `0` in
  every CSV row. `VmRSS` was misleading: kernel queue pages are not
  charged uniformly across processes, child RSS is gone after `waitpid`,
  and for fan-in (8 producers) the parent-only metric is meaningless.
  CSV column retained for backward compatibility; analysis ignores it.

### 1.4 Topology — placement axis differentiation

Old `detect_cpu_pairs.sh` exported only two pairs: `SAME_CORE_CPUS` and
`CROSS_CORE_CPUS`. On modern CPUs "cross-core" can mean very different
things (same L2 cluster, same L3 cluster, cross-CCX). It now exports up
to four axes by reading
`/sys/devices/system/cpu/cpu*/cache/index{2,3}/shared_cpu_list`:

| Axis | Meaning |
|---|---|
| `SAME_CORE_CPUS` | SMT siblings (same physical core) |
| `SAME_L2_CPUS` | Different cores, same L2 cluster |
| `SAME_L3_CPUS` | Different L2 clusters, same L3 (within socket / CCX) |
| `DIFF_L3_CPUS` | Different L3 caches (cross-CCX or cross-socket) |

Empty axes (e.g. `DIFF_L3` on a single-CCD chip) are auto-skipped by the
runner. The existing `CROSS_CORE_CPUS` is also still exported as a
backward-compatible alias to whichever non-SMT pair was found first.

### 1.5 Run-order randomisation

`run_suite.sh` rewritten. All `(benchmark, config, run_id)` tuples within
a stack are flattened into a TSV "card file" and shuffled with `shuf`
before execution (controlled by `RANDOMIZE=1` in `defaults.env`,
disable with `--no-randomize`). Removes the confound between system
drift and POSIX-vs-SysV ordering within a single stack.

### 1.6 System-state capture

`results/logs/topology.txt` now records:

- `uname -a`
- `lscpu`
- `gcc --version`
- All `/sys/devices/system/cpu/vulnerabilities/*` (mitigations status)
- `/sys/devices/system/cpu/cpufreq/boost`
- Active CPU governor
- The selected CPU pairs for every active placement axis
- `RUNS`, `PP_ITERS`, `TP_MSGS`, `SC_K`, `RANDOMIZE`

This is the system description that goes into the paper's experimental
setup section.

### 1.7 Configuration defaults

`config/defaults.env`:

| Variable | Old | New | Rationale |
|---|---|---|---|
| `RUNS` | 1 | 20 | Required for any per-cell statistical test or CI. |
| `PERF_RUNS` | 3 | 5 | More stable hardware-counter means. |
| `RANDOMIZE` | (n/a) | 1 | New, controls run-order shuffle. |

### 1.8 perf and strace coverage

`run_perf.sh` extended from 5 sections to 7, adding `shm_throughput` and
`shm_pairs`. Section labels rewritten ([1/7] … [7/7]).

`run_strace.sh` extended similarly. Two new representative configs
traced: SHM throughput (depth=128, 64B, same_core) and SHM pairs
(n=4, 64B). Total 7 strace sections per stack.

### 1.9 `analyze.py` — paper-grade rewrite

The previous `analyze.py` produced a stub report (just row counts) and
plotted error bars from data with `RUNS=1`, so the bars were always 0.
The new version produces:

**Tables (`results/analysis/tables/`):**

- `master_comparison.csv` — one row per `(family, benchmark, placement,
  msg_size, queue_depth, n_procs)` with: median POSIX, median SysV, both
  bootstrap 95% CIs, the ratio `median(SysV) / median(POSIX)` with its
  own bootstrap CI, Welch t-statistic and p-value, Mann-Whitney U
  statistic and p-value, Cliff's δ effect size, magnitude (negligible /
  small / medium / large), and the headline "winner" mechanism. This is
  the table you will reduce to a few LaTeX subtables for the paper.
- `per_config_summary.csv` — full descriptive statistics per cell:
  median, mean, std, bootstrap CI, CV per metric. Used for sanity
  checking and for filling in detailed appendix tables.
- `coverage.csv` — rows-per-cell sanity check.
- `perf_summary.csv` — auto-generated from `results/perf/*/all_perf.csv`
  if those files exist. Mean ± std per `(mechanism, benchmark, event)`.
  No manual integration needed; just rerun `analyze.py` after
  `run_perf.sh`.
- `strace_summary.csv` — parsed table of `strace -c` syscall summaries.

**Statistical methods:**

- Bootstrap 95% CI on the median (10 000 resamples by default).
- Bootstrap 95% CI on the ratio `median(SysV) / median(POSIX)` — the
  metric the paper will quote as the "speedup" headline.
- Welch's t-test (parametric) and Mann-Whitney U (non-parametric, the
  preferred test for non-Gaussian latency distributions).
- Cliff's δ effect size with magnitude bands: negligible (<0.147),
  small (<0.33), medium (<0.474), large (≥0.474).
- scipy is optional. If absent, `analyze.py` falls back to pure-numpy
  test implementations using the normal approximation. scipy is strongly
  preferred for exact p-values and is the assumed dependency in the
  paper.

**Plots (`results/analysis/figures/`):**

- `mq_pingpong_p99` / `shm_pingpong_p99` — p99 latency vs message size,
  one subplot per active placement, log-log axes, IQR band per
  mechanism.
- `mq_throughput` / `shm_throughput` — throughput (MB/s) vs message
  size, one subplot per placement, separate lines per queue depth.
- `mq_fanin` — fan-in throughput vs producer count, one subplot per
  message size.
- `mq_pairs_p99` / `shm_pairs_p99` — pair-scalability p99 latency vs
  pair count, one subplot per message size.
- `mq_tail_overlay` / `shm_tail_overlay` — p50/p95/p99/p99.9 overlay for
  the most contested config (smallest size, same-core).

All figures saved as both PNG (180 DPI) and PDF (vector) by default —
the PDFs are submission-ready.

**Narrative report (`results/analysis/analysis_report.md`):**

Auto-generated. Lists every cell where Mann-Whitney `p < 0.05` along
with its ratio, CI, p-value, Cliff's δ, and winner. This is the
short list the paper's "Results" section walks through.

---

## 2. Known limitations and threats to validity

The following are real limitations of the suite as it stands. The paper
should disclose each.

### 2.1 Single test machine

All measurements are from one Ubuntu desktop with a single CPU model.
Results characterise that CPU and that kernel build, not POSIX vs SysV
in general. We do not attempt to generalise across architectures (Arm,
RISC-V), kernels (mainline vs RT vs distro patches), or libc
implementations (glibc vs musl).

**Mitigation in paper:** Explicitly scope the claims. Report the full
system description from `topology.txt`.

### 2.2 Cross-socket / NUMA placement not measured

The test box is single socket, so `cross_socket` placement is
unavailable. We cannot quantify NUMA-crossing latency or throughput
penalties for either IPC mechanism. Multi-socket effects can be
qualitatively different — POSIX MQ message copies cross interconnects;
SysV shm pages can become NUMA-pinned to one node.

**Mitigation in paper:** Note as future work; cite prior NUMA-IPC
literature for the qualitative direction.

### 2.3 No mitigation comparison

We run with the kernel mitigations in whatever state Ubuntu provides
(captured in `topology.txt`). We do not produce a paired
"mitigations off" baseline. Mitigations such as KPTI/Retbleed materially
inflate syscall cost, so absolute numbers are tied to the mitigation
regime in effect.

**Mitigation in paper:** Report the captured `vulnerabilities/*` output
and note that absolute syscall cost is mitigation-dependent. The
**relative** POSIX-vs-SysV comparison is largely insensitive to
mitigations because both sides pay them.

### 2.4 No CPU isolation in the default suite

`run_suite.sh` sets `scaling_governor=performance` and lifts ulimits but
does not boot-time-isolate cores. Background scheduler interference,
IRQ handling, and turbo-boost variance therefore leak into the timing.
On the recommended Ubuntu run we suggest `isolcpus=`, IRQ affinity off
the test cores, and `cpufreq/boost=0`, but those are operator actions
documented in this file rather than enforced by the script.

**Mitigation in paper:** Report what was applied for the cited run
(boot cmdline, IRQ map, boost state). If isolation was not applied,
say so explicitly and note the wider variance as a threat.

### 2.5 Cross-stack run order is not randomised

Within a stack, all `(config, run_id)` tuples are shuffled. Across
stacks the order is fixed (POSIX first, then SysV). System drift over
the multi-hour suite therefore affects POSIX and SysV slightly
differently. Per-stack noise is independent, but a long-term linear
drift would be picked up only by SysV.

**Mitigation in paper:** Report total wall time. If the stacks took
similar time, the residual drift is small.

**Future fix:** unify the two stacks under a single shuffled card list.
This is straightforward but requires re-architecting the runner — left
for the next iteration.

### 2.6 `scalability` binary's CPU pinning is hardcoded round-robin

The `scalability` binary pins each child to `i % num_cpus()` rather than
exposing a placement flag. On a single-socket desktop with isolcpus this
is acceptable; on a larger box it does not let us study placement
effects in fan-in / pairs.

**Mitigation in paper:** Report the chosen scheme; note that scalability
results are entropy-bound by the OS scheduler when N exceeds isolated
core count.

### 2.7 SHM fan-in not implemented

We implemented SHM ping-pong, throughput, and parallel pairs. We did
**not** implement an SHM fan-in benchmark. Multi-producer single-consumer
shared-memory queues require either a multi-producer ring with atomic
indices and proper memory ordering (introduces its own contention
artifacts) or per-producer slots with a consumer round-robin (different
benchmark semantics).

**Mitigation in paper:** State as future work. Report MQ fan-in only,
with the caveat that the SHM equivalent is non-trivial to implement
fairly.

### 2.8 No raw-sample dump for CDF plots

The C binaries compute p50/p95/p99/p99.9 internally and emit only
summary statistics in CSV. We cannot draw an exact tail-latency CDF
because the per-iteration latencies are not preserved. Our
`tail_overlay` plot therefore overlays the four percentile points per
mechanism, not a full CDF.

**Mitigation in paper:** State the limitation; show the percentile
overlay as the closest available approximation. **Future work:** add a
`--dump-samples FILE` flag to the C binaries that writes raw `uint64_t`
samples for offline CDF construction.

### 2.9 Compiler / glibc tuning is default

Both stacks compile with `-O2 -Wall -Wextra -D_GNU_SOURCE`. We do not
explore `-O3`, link-time optimisation, profile-guided optimisation, or
alternate libcs. The MQ syscall path is not in the hot loop after
queue creation, so compiler opts have small effect, but we have not
verified this empirically.

**Mitigation in paper:** Report compiler version and flags.

### 2.10 `RUNS=20` is the practical minimum

Twenty runs gives bootstrap CIs that are usable but wide. For very
close cells (e.g. small differences at large message sizes) the CI
overlap will obscure a small-but-real effect. Thirty or fifty runs
would tighten the bands; the suite supports it via `--runs N`.

**Mitigation in paper:** Report `RUNS` used; if any cells have
overlapping CIs, present them as "no detected difference" rather than
"equivalent".

### 2.11 No long-tail measurement

All ping-pong runs are 50 000 iterations. We see p99.9 reliably but not
p99.99 or beyond. For real-time / low-tail-latency claims a different
methodology is needed (HdrHistogram, longer runs, separate tail-only
benchmarks).

**Mitigation in paper:** Limit p99.9 claims to "tail-of-fifty-thousand";
do not extrapolate to higher quantiles.

### 2.12 No multi-message-priority study

POSIX MQ supports message priorities (heap insertion); SysV MQ supports
message types (filtered FIFO). Our tests use a single priority / mtype
to keep the baseline comparable, so we do not measure the priority-
queueing overhead that POSIX MQ pays even when only one priority is in
use. There is a `priority/` directory in each stack that hints at a
priority benchmark, but the test was not wired into this suite.

**Mitigation in paper:** Discuss priority handling as a qualitative
design difference; defer measured comparison to future work.

### 2.13 Synthetic workload only

Every test uses fixed-size, no-think-time message streams. Real
applications have variable size, bursty arrivals, mixed read/write
patterns, and computation between messages. Our results characterise
the IPC mechanism, not application throughput.

**Mitigation in paper:** Explicit in the methodology section.

### 2.14 Memory measurement removed entirely

We removed the `VmRSS` delta column rather than measure memory more
carefully. A complete IPC paper would measure memory use, but the
correct way is via cgroup memory accounting or direct kernel slab
inspection — both out of scope for this revision.

**Mitigation in paper:** Note as future work; cite kernel docs on
where MQ pages are charged.

---

## 3. Future work (worth listing in the paper)

1. **NUMA / cross-socket placement** — extend with a multi-socket box.
2. **Mitigations on/off paired study** — same hardware, two boot
   configs, paired runs, report the relative POSIX-vs-SysV invariance.
3. **SHM fan-in** — multi-producer ring or per-producer slot designs.
4. **Raw-sample CDF** — `--dump-samples` flag, full HdrHistogram.
5. **Priority-mix study** — vary the number of priorities / mtypes,
   measure the ordering-overhead tax.
6. **Cross-architecture** — repeat on Arm Neoverse, AMD Zen, Intel
   hybrid; the kernel queue lock contention story differs by core
   count and L3 topology.
7. **Cross-libc** — repeat with musl; POSIX MQ uses the rt library on
   glibc, may be implemented differently elsewhere.
8. **Kernel-level instrumentation** — trace `mq_send` / `msgsnd`
   internals with eBPF; quantify time spent in lock vs copy vs wakeup.
9. **Memory-pressure regimes** — repeat under cgroup memory
   constraints; SysV shm pin behaviour differs from POSIX shm under
   pressure.
10. **Real workload** — replay an application IPC trace (e.g. nginx
    worker IPC) instead of synthetic streams.

---

## 4. Threats to validity — short version for the paper

A condensed list suitable for a "Threats to validity" subsection:

> *Internal:* Single test machine, fixed kernel and libc, ulimits and
> sysctls captured in `topology.txt`. CPU governor pinned to
> `performance`; isolcpus / IRQ affinity / boost-disable applied per
> the boot cmdline reported in the artefact.
> *External:* Single-socket desktop, no NUMA crossing measured. Single
> mitigations regime. Synthetic fixed-size workload. Compiler at `-O2`,
> default glibc.
> *Construct:* Latency measured at the producer / initiator side via
> `CLOCK_MONOTONIC_RAW`. Throughput measured wall-clock around the
> producer loop. Memory-footprint measurement intentionally omitted
> (the available `VmRSS` proxy is not a reliable IPC-memory metric).
> *Conclusion:* `RUNS=20` per cell with bootstrap 95% CIs; tests are
> Mann-Whitney U (primary, non-parametric) and Welch's t (secondary).
> Effect sizes reported via Cliff's δ. Cells with overlapping 95% CIs
> are reported as "no detected difference" rather than equivalent.
