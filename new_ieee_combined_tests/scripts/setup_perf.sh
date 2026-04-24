#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

POSIX_DIR="${BENCH_POSIX_DIR:-${ROOT_DIR}/stacks/POSIX}"
SYSV_DIR="${BENCH_SYSV_DIR:-${ROOT_DIR}/stacks/SYSV}"

[[ -f "${POSIX_DIR}/Makefile" ]] || {
    echo "Missing POSIX Makefile at ${POSIX_DIR}" >&2
    exit 1
}

[[ -f "${SYSV_DIR}/Makefile" ]] || {
    echo "Missing SYSV Makefile at ${SYSV_DIR}" >&2
    exit 1
}

echo "[setup_perf] POSIX stack"
make -C "$POSIX_DIR" setup_perf

echo "[setup_perf] SYSV stack"
make -C "$SYSV_DIR" setup_perf

echo "[setup_perf] done"
