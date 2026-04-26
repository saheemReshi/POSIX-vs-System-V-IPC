SHELL := /bin/bash

.PHONY: setup-sysctl setup-perf run run-posix run-sysv quick analyze perf strace all clean

setup-sysctl:
	bash scripts/setup_sysctl.sh

setup-perf:
	bash scripts/setup_perf.sh

run:
	bash scripts/run_suite.sh --only both

run-posix:
	bash scripts/run_suite.sh --only posix

run-sysv:
	bash scripts/run_suite.sh --only sysv

quick:
	bash scripts/run_suite.sh --quick --only both

analyze:
	python3 scripts/analyze.py

perf:
	bash scripts/run_perf.sh --stack both

strace:
	bash scripts/run_strace.sh --stack both

all: run analyze

clean:
	rm -f results/raw/posix/all_runs.csv
	rm -f results/raw/sysv/all_runs.csv
	rm -f results/raw/combined/all_runs.csv
	rm -rf results/analysis/figures/*
	rm -rf results/analysis/tables/*
	rm -f results/analysis/analysis_report.md
	rm -f results/logs/*.log results/logs/topology.txt
	@if [[ -d stacks/POSIX ]]; then $(MAKE) -C stacks/POSIX clean; fi
	@if [[ -d stacks/SYSV ]]; then $(MAKE) -C stacks/SYSV clean; fi
