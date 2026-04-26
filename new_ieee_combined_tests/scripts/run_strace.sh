#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────
# run_strace.sh – Collect strace syscall summaries for representative
#                 benchmark configurations (not the full matrix).
#
# Output:  results/strace/posix/strace_summary.txt
#          results/strace/sysv/strace_summary.txt
#
# Configurations (deliberately small for readability):
#   MQ pingpong   – 64B, same_core + cross_core
#   SHM pingpong  – 64B, same_core + cross_core
#   MQ throughput  – 64B, depth=1 + depth=128, same_core
#   MQ fanin       – n=8, 64B
#   MQ pairs       – n=4, 64B
# ──────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DEFAULT_POSIX_DIR="${ROOT_DIR}/stacks/POSIX"
DEFAULT_SYSV_DIR="${ROOT_DIR}/stacks/SYSV"

POSIX_DIR="${BENCH_POSIX_DIR:-${DEFAULT_POSIX_DIR}}"
SYSV_DIR="${BENCH_SYSV_DIR:-${DEFAULT_SYSV_DIR}}"
CONFIG_FILE="${ROOT_DIR}/config/defaults.env"

[[ -f "$CONFIG_FILE" ]] || {
    echo "Missing config file: ${CONFIG_FILE}" >&2
    exit 1
}

# shellcheck disable=SC1090
source "$CONFIG_FILE"

# ── strace availability ──────────────────────────────────────────────
if ! command -v strace &>/dev/null; then
    echo "ERROR: 'strace' is not installed or not in PATH." >&2
    echo "Install it with:  sudo apt install strace" >&2
    exit 1
fi

# ── CLI defaults ─────────────────────────────────────────────────────
ONLY_STACK="both"      # both | posix | sysv
DO_BUILD=1
QUICK=0
DO_SETUP_SYSCTL=1

# Allow env-level override; fall back to config's STRACE_RUNS (or 1).
STRACE_RUNS_VAL="${STRACE_RUNS:-1}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stack|--only)
            shift; ONLY_STACK="$1" ;;
        --runs)
            shift; STRACE_RUNS_VAL="$1" ;;
        --quick)
            QUICK=1 ;;
        --no-build)
            DO_BUILD=0 ;;
        --skip-setup-sysctl)
            DO_SETUP_SYSCTL=0 ;;
        --help)
            cat <<EOF
Usage: bash scripts/run_strace.sh [options]

Collects strace -c syscall summaries for representative benchmark
configurations.  Produces one readable file per stack.

Output:
  results/strace/posix/strace_summary.txt
  results/strace/sysv/strace_summary.txt

Configurations traced:
  MQ pingpong   – 64B, same_core + cross_core
  SHM pingpong  – 64B, same_core + cross_core
  MQ throughput  – 64B, depth 1 + 128, same_core only
  MQ fanin       – n=8, 64B
  MQ pairs       – n=4, 64B

Options:
  --stack {both|posix|sysv}  Run selected stack only (default: both)
  --runs  N                  Repetitions per config (default: STRACE_RUNS from env / 1)
  --quick                    Fast smoke mode (small iteration counts)
  --no-build                 Skip 'make all' in source stacks
  --skip-setup-sysctl        Skip kernel IPC sysctl setup
  --help                     Show this help

Environment overrides:
  STRACE_RUNS=N              Same as --runs (CLI takes priority)
EOF
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
    shift
done

if [[ "$ONLY_STACK" != "both" && "$ONLY_STACK" != "posix" && "$ONLY_STACK" != "sysv" ]]; then
    echo "Invalid --stack value: ${ONLY_STACK}" >&2
    exit 1
fi

if [[ "$QUICK" -eq 1 ]]; then
    PP_ITERS="$QUICK_PP_ITERS"
    TP_MSGS="$QUICK_TP_MSGS"
    SC_K="$QUICK_SC_K"
fi

if ! [[ "$STRACE_RUNS_VAL" =~ ^[0-9]+$ ]] || [[ "$STRACE_RUNS_VAL" -lt 1 ]]; then
    echo "STRACE_RUNS must be a positive integer." >&2
    exit 1
fi

[[ -d "$POSIX_DIR" ]] || { echo "POSIX stack dir not found: ${POSIX_DIR}" >&2; exit 1; }
[[ -d "$SYSV_DIR"  ]] || { echo "SYSV stack dir not found: ${SYSV_DIR}"  >&2; exit 1; }

# ── system setup (mirrors run_suite.sh) ──────────────────────────────
SUDO_NONINTERACTIVE_OK=0
if sudo -n true >/dev/null 2>&1; then
    SUDO_NONINTERACTIVE_OK=1
fi

if [[ "$DO_SETUP_SYSCTL" -eq 1 ]]; then
    echo "Applying kernel IPC limits (setup_sysctl)..."
    bash "${ROOT_DIR}/scripts/setup_sysctl.sh"
fi

# ── raise resource limits ────────────────────────────────────────────
TARGET_NOFILE="${BENCH_NOFILE_LIMIT:-65536}"
TARGET_MSGQUEUE_BYTES="${BENCH_MSGQUEUE_LIMIT:-67108864}"

echo "Setting open file descriptor limit to ${TARGET_NOFILE}..."
if ! ulimit -n "$TARGET_NOFILE" 2>/dev/null; then
    if command -v prlimit >/dev/null 2>&1 && [[ "$SUDO_NONINTERACTIVE_OK" -eq 1 ]]; then
        if sudo -n prlimit --pid $$ --nofile="${TARGET_NOFILE}:${TARGET_NOFILE}" >/dev/null 2>&1 \
           && ulimit -n "$TARGET_NOFILE" 2>/dev/null; then
            echo "Raised RLIMIT_NOFILE via sudo prlimit."
        else
            echo "Warning: could not set file descriptor limit to ${TARGET_NOFILE} (current: $(ulimit -n))." >&2
        fi
    else
        echo "Warning: could not set file descriptor limit to ${TARGET_NOFILE} (current: $(ulimit -n))." >&2
    fi
fi

echo "Setting POSIX message-queue byte limit to ${TARGET_MSGQUEUE_BYTES}..."
if ! ulimit -q "$TARGET_MSGQUEUE_BYTES" 2>/dev/null; then
    if command -v prlimit >/dev/null 2>&1 && [[ "$SUDO_NONINTERACTIVE_OK" -eq 1 ]]; then
        if sudo -n prlimit --pid $$ --msgqueue="${TARGET_MSGQUEUE_BYTES}:${TARGET_MSGQUEUE_BYTES}" >/dev/null 2>&1 \
           && ulimit -q "$TARGET_MSGQUEUE_BYTES" 2>/dev/null; then
            echo "Raised RLIMIT_MSGQUEUE via sudo prlimit."
        else
            echo "Warning: could not set RLIMIT_MSGQUEUE to ${TARGET_MSGQUEUE_BYTES} (current: $(ulimit -q))." >&2
        fi
    else
        echo "Warning: could not set RLIMIT_MSGQUEUE to ${TARGET_MSGQUEUE_BYTES} (current: $(ulimit -q))." >&2
    fi
fi

echo "Effective limits: nofile=$(ulimit -n), msgqueue=$(ulimit -q)"

# ── CPU governor ─────────────────────────────────────────────────────
echo "Setting CPU governor to performance (avoid freq scaling)..."

if [ -d "/sys/devices/system/cpu/cpu0/cpufreq" ]; then
    if [[ "$SUDO_NONINTERACTIVE_OK" -eq 1 ]]; then
        if echo performance | sudo -n tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null; then
            echo "CPU governor set to performance."
        else
            echo "Warning: failed to set CPU governor to performance." >&2
        fi
    else
        echo "Warning: sudo credentials unavailable; skipping CPU governor setup." >&2
        echo "         Run 'sudo -v' before this script if you want governor control." >&2
    fi

    gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
    echo "Current governor: $gov"
else
    echo "WARNING: cpufreq interface not found. Skipping CPU governor setup." >&2
fi

# ── CPU topology ─────────────────────────────────────────────────────
eval "$("${ROOT_DIR}/scripts/detect_cpu_pairs.sh" --export)"

TOTAL_RUNS=0

# ── helper ───────────────────────────────────────────────────────────
#
# strace_run <outfile> <benchmark> <placement> <detail> <binary> [args...]
#
#   outfile   – path to the single summary file (append mode)
#   benchmark – human label, e.g. "MQ pingpong"
#   placement – e.g. "same_core", or "" if not applicable
#   detail    – extra info, e.g. "64B" or "depth=128 64B"
#   binary    – path to the benchmark executable
#   [args...] – CLI arguments forwarded to the binary
#
strace_run() {
    local outfile="$1"; shift
    local benchmark="$1"; shift
    local placement="$1"; shift
    local detail="$1"; shift
    local bin="$1"; shift

    local header="${benchmark}"
    [[ -n "$placement" ]] && header="${header} | ${placement}"
    [[ -n "$detail" ]]    && header="${header} | ${detail}"

    printf "    %-62s " "${header} ..."

    for ((r=1; r<=STRACE_RUNS_VAL; r++)); do
        {
            echo ""
            echo "============================================================"
            echo "${header}"
            echo "============================================================"
            echo "# Binary : $bin"
            echo "# Args   : $*"
            echo "# Run    : ${r}/${STRACE_RUNS_VAL}"
            echo "# Date   : $(date -Iseconds)"
            echo ""
        } >> "$outfile"

        # strace -c writes its summary table to stderr.
        # Benchmark stdout is discarded; stderr (strace output) is captured.
        strace -c -S time -f -- "$bin" -r "$r" "$@" \
            >>/dev/null 2>>"$outfile" || true

        echo "" >> "$outfile"

        TOTAL_RUNS=$((TOTAL_RUNS + 1))
    done

    echo "done (${STRACE_RUNS_VAL} runs)"
}

# ── per-stack driver ─────────────────────────────────────────────────
run_stack_strace() {
    local stack="$1"
    local stack_dir=""

    if [[ "$stack" == "posix" ]]; then
        stack_dir="$POSIX_DIR"
    else
        stack_dir="$SYSV_DIR"
    fi

    local bindir="${stack_dir}/bin"
    local outdir="${ROOT_DIR}/results/strace/${stack}"
    local outfile="${outdir}/strace_summary.txt"
    local log="${ROOT_DIR}/results/logs/${stack}_strace.log"

    mkdir -p "$outdir" "${ROOT_DIR}/results/logs"
    : > "$log"

    # Initialise output file with header
    {
        echo "╔══════════════════════════════════════════════════════════════╗"
        echo "║  STRACE SYSCALL SUMMARY — ${stack^^}"
        printf "║  %-60s║\n" "Generated: $(date -Iseconds)"
        printf "║  %-60s║\n" "Runs per config: ${STRACE_RUNS_VAL}"
        echo "╚══════════════════════════════════════════════════════════════╝"
    } > "$outfile"

    # Build
    if [[ "$DO_BUILD" -eq 1 ]]; then
        echo "[${stack}] Building binaries (make all)"
        make -C "$stack_dir" all >> "$log" 2>&1
    fi

    # Validate binaries
    for b in pingpong throughput scalability shm_pingpong; do
        [[ -x "${bindir}/${b}" ]] || {
            echo "Missing binary: ${bindir}/${b}" >&2
            echo "Check build logs at ${log}" >&2
            exit 1
        }
    done

    echo ""
    echo "============================================================"
    echo "[${stack}] Collecting strace syscall summaries"
    echo "============================================================"

    # ── [1/5] MQ pingpong ───────────────────────────────────────────
    echo "[1/5] MQ pingpong (64B, same_core + cross_core)"
    strace_run "$outfile" "MQ pingpong" "same_core" "64B" \
        "${bindir}/pingpong" \
        -n "$PP_ITERS" -s 64 -c "$SAME_CORE_CPUS" -p same_core

    strace_run "$outfile" "MQ pingpong" "cross_core" "64B" \
        "${bindir}/pingpong" \
        -n "$PP_ITERS" -s 64 -c "$CROSS_CORE_CPUS" -p cross_core

    # ── [2/5] SHM pingpong ──────────────────────────────────────────
    echo "[2/5] SHM pingpong (64B, same_core + cross_core)"
    strace_run "$outfile" "SHM pingpong" "same_core" "64B" \
        "${bindir}/shm_pingpong" \
        -n "$PP_ITERS" -s 64 -c "$SAME_CORE_CPUS" -p same_core

    strace_run "$outfile" "SHM pingpong" "cross_core" "64B" \
        "${bindir}/shm_pingpong" \
        -n "$PP_ITERS" -s 64 -c "$CROSS_CORE_CPUS" -p cross_core

    # ── [3/5] MQ throughput ─────────────────────────────────────────
    echo "[3/5] MQ throughput (64B, depth=1 + depth=128, same_core)"
    strace_run "$outfile" "MQ throughput" "same_core" "depth=1 64B" \
        "${bindir}/throughput" \
        -n "$TP_MSGS" -s 64 -q 1 -c "$SAME_CORE_CPUS" -p same_core_q1

    strace_run "$outfile" "MQ throughput" "same_core" "depth=128 64B" \
        "${bindir}/throughput" \
        -n "$TP_MSGS" -s 64 -q 128 -c "$SAME_CORE_CPUS" -p same_core_q128

    # ── [4/5] MQ scalability fan-in ─────────────────────────────────
    echo "[4/5] MQ scalability fan-in (n=8, 64B)"
    strace_run "$outfile" "MQ scalability fan-in" "" "n=8 64B" \
        "${bindir}/scalability" \
        -m c1 -n 8 -k "$SC_K" -s 64 -p fanin_n8

    # ── [5/5] MQ scalability pairs ──────────────────────────────────
    echo "[5/5] MQ scalability pairs (n=4, 64B)"
    strace_run "$outfile" "MQ scalability pairs" "" "n=4 64B" \
        "${bindir}/scalability" \
        -m c2 -n 4 -k "$SC_K" -s 64 -p pairs_n4

    echo ""
    echo "[${stack}] complete -> ${outfile}"
    echo "[${stack}] log      -> ${log}"
}

# ── main ─────────────────────────────────────────────────────────────
if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "posix" ]]; then
    run_stack_strace posix
fi

if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "sysv" ]]; then
    run_stack_strace sysv
fi

echo ""
echo "════════════════════════════════════════════════════════════════"
echo "  STRACE COLLECTION SUMMARY"
echo "════════════════════════════════════════════════════════════════"
echo "  Total benchmark invocations : ${TOTAL_RUNS}"
echo "  Runs per configuration      : ${STRACE_RUNS_VAL}"
echo "  Output files                :"
if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "posix" ]]; then
    echo "    results/strace/posix/strace_summary.txt"
fi
if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "sysv" ]]; then
    echo "    results/strace/sysv/strace_summary.txt"
fi
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "Done.  Compare the two files side-by-side for POSIX vs SysV syscall profiles."
