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

if ! sudo -n true >/dev/null 2>&1; then
    echo "[setup_perf] warning: sudo credentials unavailable; skipping perf sysctl setup." >&2
    echo "[setup_perf] run 'sudo -v' and rerun this script to apply perf limits." >&2
    exit 0
fi

echo "[setup_perf] POSIX stack"
make -C "$POSIX_DIR" setup_perf

echo "[setup_perf] SYSV stack"
make -C "$SYSV_DIR" setup_perf

echo "[setup_perf] done"
