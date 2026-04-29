SHELL := /bin/bash

.PHONY: setup-sysctl setup-perf run run-posix run-sysv quick analyze perf strace report report-clean all clean

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

report:
	@if [ ! -f results/analysis/figures/mq_pingpong_ratio_p50.pdf ]; then \
	    echo "WARNING: figures missing under results/analysis/figures/."; \
	    echo "         Run 'make analyze' first to generate them."; \
	    echo "         Continuing anyway — PDF will have placeholder boxes."; \
	fi
	-pdflatex -interaction=nonstopmode REPORT.tex
	-bibtex REPORT
	-pdflatex -interaction=nonstopmode REPORT.tex
	-pdflatex -interaction=nonstopmode REPORT.tex
	@if [ -f REPORT.pdf ]; then \
	    echo ""; \
	    echo "Report -> REPORT.pdf  ($$(du -h REPORT.pdf | cut -f1))"; \
	    echo ""; \
	    echo "Sanity: any unresolved references?"; \
	    grep -c "Reference.*undefined" REPORT.log 2>/dev/null \
	        | (read n; echo "  Undefined references: $$n"); \
	    grep -c "Citation.*undefined" REPORT.log 2>/dev/null \
	        | (read n; echo "  Undefined citations:  $$n"); \
	    grep -c "File.*not found" REPORT.log 2>/dev/null \
	        | (read n; echo "  Missing figures:      $$n"); \
	fi

report-clean:
	rm -f REPORT.aux REPORT.bbl REPORT.blg REPORT.log REPORT.out REPORT.toc

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
