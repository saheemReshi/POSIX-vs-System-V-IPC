# Compact POSIX vs SysV Benchmark Suite

This folder contains a fresh benchmark harness focused on **POSIX vs System V IPC** comparison with a simpler topology model:

- `same_core`: two logical CPUs on one physical core (SMT siblings when available)
- `cross_core`: two CPUs on different cores (same socket preference)

No NUMA / cross-socket placement is used.

## What Is Included

### Tests (exactly 5)

1. MQ ping-pong latency (`pingpong`)
2. MQ throughput (`throughput`)
3. MQ scalability fan-in (`scalability -m c1`)
4. MQ scalability parallel pairs (`scalability -m c2`)
5. SHM ping-pong baseline (`shm_pingpong`)

### Folder organization

```text
new_ieee_combined_tests/
  config/
    defaults.env                 # run counts and test matrix
  stacks/
    POSIX/                       # vendored POSIX source stack
    SYSV/                        # vendored SysV source stack
  scripts/
    detect_cpu_pairs.sh          # same_core / cross_core detection
    setup_sysctl.sh              # apply MQ/System-V kernel limits
    setup_perf.sh                # apply perf kernel limits
    run_suite.sh                 # run POSIX and/or SysV benchmarks
    combine_results.sh           # merge raw POSIX + SysV CSVs
    analyze.py                   # analysis tables + figures + report
  results/
    raw/
      posix/all_runs.csv
      sysv/all_runs.csv
      combined/all_runs.csv
    analysis/
      figures/
      tables/
      analysis_report.md
    logs/
      topology.txt
      posix_run.log
      sysv_run.log
  Makefile
  README.md
```

## Prerequisites

- `make`, `bash`, `python3`
- Python packages for analysis:
  - `pandas`
  - `matplotlib`

This folder is self-contained: source stacks are available locally in:

- `stacks/POSIX`
- `stacks/SYSV`

Install analysis dependencies if needed:

```bash
python3 -m pip install pandas matplotlib
```

## Exact Run Instructions

Run these commands exactly from the benchmark root:

```bash
cd /home/saheem/Desktop/iiitb/imt2023_sem6/linux_ipc_pe/posix_vs_sysv/new_ieee_combined_tests
```

1. Apply kernel IPC limits (required for MQ scalability/throughput, will also happen by default when run_suite.sh is run):

```bash
bash scripts/setup_sysctl.sh
```

1. Optional (only needed if you will run perf profiling):

```bash
bash scripts/setup_perf.sh
```

1. Run benchmark suite:

```bash
bash scripts/run_suite.sh
```

1. Run analysis:

```bash
python3 scripts/analyze.py
```

1. (Optional) one-command shortcuts:

```bash
make run
make analyze
```

## Run Benchmarks

From this folder:

```bash
bash scripts/run_suite.sh
```

Useful options:

```bash
bash scripts/run_suite.sh --quick
bash scripts/run_suite.sh --only posix
bash scripts/run_suite.sh --only sysv
bash scripts/run_suite.sh --runs 30
bash scripts/run_suite.sh --no-build
bash scripts/run_suite.sh --skip-setup-sysctl
bash scripts/run_suite.sh --setup-perf
```

Or use Make targets:

```bash
make run
make quick
```

## Run analysis

```bash
python3 scripts/analyze.py
```

Optional flags:

```bash
python3 scripts/analyze.py --format both
python3 scripts/analyze.py --input results/raw/combined/all_runs.csv
python3 scripts/analyze.py --outdir results/analysis
```

Or:

```bash
make analyze
```

## Environment File Reference

`config/defaults.env` controls run counts and test matrix:

- `RUNS`: repetitions per configuration.
- `PP_ITERS`: ping-pong iterations per run.
- `TP_MSGS`: throughput messages per run.
- `SC_K`: messages/pair (or producer) for scalability tests.
- `PINGPONG_SIZES`: payload sizes for ping-pong tests.
- `THROUGHPUT_SIZES`: payload sizes for throughput tests.
- `THROUGHPUT_DEPTHS`: queue depths for throughput tests.
- `SCALABILITY_SIZES`: payload sizes for scalability tests.
- `FANIN_N`: producer counts for fan-in test (`c1`).
- `PAIRS_N`: pair counts for parallel-pairs test (`c2`).
- `QUICK_RUNS`: run count in quick mode.
- `QUICK_PP_ITERS`: ping-pong iterations in quick mode.
- `QUICK_TP_MSGS`: throughput messages in quick mode.
- `QUICK_SC_K`: scalability messages in quick mode.

## Script Reference

- `scripts/setup_sysctl.sh`:
  applies required kernel IPC limits by calling `setup_sysctl` in both vendored stacks.

- `scripts/setup_perf.sh`:
  applies perf-related kernel settings by calling `setup_perf` in both vendored stacks.

- `scripts/detect_cpu_pairs.sh`:
  detects and exports CPU pairs for `same_core` and `cross_core` placements.

- `scripts/run_suite.sh`:
  builds stacks, applies setup (sysctl by default), runs all 5 tests, writes raw CSV/logs, then combines POSIX and SysV CSVs.

- `scripts/combine_results.sh`:
  merges `results/raw/posix/all_runs.csv` and `results/raw/sysv/all_runs.csv` into `results/raw/combined/all_runs.csv`.

- `scripts/analyze.py`:
  reads combined CSV and generates summary tables, figures, and `analysis_report.md`.

## Output summary

- `results/raw/combined/all_runs.csv`: merged POSIX + SysV benchmark rows
- `results/analysis/tables/summary_by_config.csv`: grouped stats by mechanism/test/config
- `results/analysis/tables/coverage_by_test.csv`: row/run coverage per test
- `results/analysis/figures/*`: comparison plots
- `results/analysis/analysis_report.md`: compact textual summary

## Notes

- `scalability` binaries do not expose a CPU-pair option (`-c`), so placement is directly applied to ping-pong and throughput tests.
- CPU pair selection is logged in `results/logs/topology.txt`.
