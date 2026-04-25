#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DEFAULT_POSIX_DIR="${ROOT_DIR}/stacks/POSIX"
DEFAULT_SYSV_DIR="${ROOT_DIR}/stacks/SYSV"

# Allow advanced override through environment variables.
POSIX_DIR="${BENCH_POSIX_DIR:-${DEFAULT_POSIX_DIR}}"
SYSV_DIR="${BENCH_SYSV_DIR:-${DEFAULT_SYSV_DIR}}"
CONFIG_FILE="${ROOT_DIR}/config/defaults.env"

[[ -f "$CONFIG_FILE" ]] || {
    echo "Missing config file: ${CONFIG_FILE}" >&2
    exit 1
}

# shellcheck disable=SC1090
source "$CONFIG_FILE"

ONLY_STACK="both"      # both | posix | sysv
DO_BUILD=1
QUICK=0
DO_SETUP_SYSCTL=1
DO_SETUP_PERF=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only)
            shift
            ONLY_STACK="$1"
            ;;
        --runs)
            shift
            RUNS="$1"
            ;;
        --quick)
            QUICK=1
            ;;
        --no-build)
            DO_BUILD=0
            ;;
        --skip-setup-sysctl)
            DO_SETUP_SYSCTL=0
            ;;
        --setup-perf)
            DO_SETUP_PERF=1
            ;;
        --help)
            cat <<EOF
Usage: bash scripts/run_suite.sh [options]

Runs a compact benchmark suite (4 MQ tests + 1 SHM test) for POSIX and/or SysV.
CPU placement is restricted to: same_core and cross_core.

Options:
  --only {both|posix|sysv}   Run selected stack only (default: both)
  --runs N                   Repetitions per configuration
  --quick                    Fast smoke mode (small counts)
  --no-build                 Skip 'make all' in source stacks
  --skip-setup-sysctl        Skip kernel IPC sysctl setup (not recommended)
  --setup-perf               Also apply perf sysctl setup
  --help                     Show this help
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
    echo "Invalid --only value: ${ONLY_STACK}" >&2
    exit 1
fi

if [[ "$QUICK" -eq 1 ]]; then
    RUNS="$QUICK_RUNS"
    PP_ITERS="$QUICK_PP_ITERS"
    TP_MSGS="$QUICK_TP_MSGS"
    SC_K="$QUICK_SC_K"
fi

if ! [[ "$RUNS" =~ ^[0-9]+$ ]] || [[ "$RUNS" -lt 1 ]]; then
    echo "RUNS must be a positive integer." >&2
    exit 1
fi

[[ -d "$POSIX_DIR" ]] || {
    echo "POSIX stack directory not found: ${POSIX_DIR}" >&2
    exit 1
}

[[ -d "$SYSV_DIR" ]] || {
    echo "SYSV stack directory not found: ${SYSV_DIR}" >&2
    exit 1
}

mkdir -p "${ROOT_DIR}/results/raw/posix" \
         "${ROOT_DIR}/results/raw/sysv" \
         "${ROOT_DIR}/results/raw/combined" \
         "${ROOT_DIR}/results/logs"

SUDO_NONINTERACTIVE_OK=0
if sudo -n true >/dev/null 2>&1; then
    SUDO_NONINTERACTIVE_OK=1
fi

if [[ "$DO_SETUP_SYSCTL" -eq 1 ]]; then
    echo "Applying kernel IPC limits (setup_sysctl)..."
    bash "${ROOT_DIR}/scripts/setup_sysctl.sh"
fi

if [[ "$DO_SETUP_PERF" -eq 1 ]]; then
    echo "Applying perf limits (setup_perf)..."
    bash "${ROOT_DIR}/scripts/setup_perf.sh"
fi

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

    # Verify (important)
    gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
    echo "Current governor: $gov"
else
    echo "WARNING: cpufreq interface not found. Skipping CPU governor setup." >&2
fi


eval "$("${ROOT_DIR}/scripts/detect_cpu_pairs.sh" --export)"

TOPO_LOG="${ROOT_DIR}/results/logs/topology.txt"
{
    echo "Generated: $(date)"
    "${ROOT_DIR}/scripts/detect_cpu_pairs.sh" --summary
    echo "RUNS=${RUNS}"
    echo "PP_ITERS=${PP_ITERS}"
    echo "TP_MSGS=${TP_MSGS}"
    echo "SC_K=${SC_K}"
} > "$TOPO_LOG"

read -r -a PINGPONG_SIZES_ARR <<< "$PINGPONG_SIZES"
read -r -a THROUGHPUT_SIZES_ARR <<< "$THROUGHPUT_SIZES"
read -r -a THROUGHPUT_DEPTHS_ARR <<< "$THROUGHPUT_DEPTHS"
read -r -a SCALABILITY_SIZES_ARR <<< "$SCALABILITY_SIZES"
read -r -a FANIN_N_ARR <<< "$FANIN_N"
read -r -a PAIRS_N_ARR <<< "$PAIRS_N"

PLACE_LABELS=("same_core" "cross_core")
PLACE_CPUS=("$SAME_CORE_CPUS" "$CROSS_CORE_CPUS")

run_n_times() {
    local csv="$1"; shift
    local log="$1"; shift
    local bin="$1"; shift
    local desc="$1"; shift

    printf "    %-62s " "$desc ..."
    for ((r=1; r<=RUNS; r++)); do
        "$bin" -r "$r" "$@" >> "$csv" 2>> "$log"
    done
    echo "done (${RUNS} runs)"
}

run_stack() {
    local stack="$1"
    local stack_dir=""

    if [[ "$stack" == "posix" ]]; then
        stack_dir="$POSIX_DIR"
    else
        stack_dir="$SYSV_DIR"
    fi

    local bindir="${stack_dir}/bin"
    local csv="${ROOT_DIR}/results/raw/${stack}/all_runs.csv"
    local log="${ROOT_DIR}/results/logs/${stack}_run.log"

    : > "$log"

    if [[ "$DO_BUILD" -eq 1 ]]; then
        echo "[${stack}] Building binaries (make all)"
        make -C "$stack_dir" all >> "$log" 2>&1
    fi

    for b in pingpong throughput scalability shm_pingpong; do
        [[ -x "${bindir}/${b}" ]] || {
            echo "Missing binary: ${bindir}/${b}" >&2
            echo "Check build logs at ${log}" >&2
            exit 1
        }
    done

    "${bindir}/pingpong" -H > "$csv"

    echo ""
    echo "============================================================"
    echo "[${stack}] Running benchmark suite"
    echo "============================================================"

    echo "[1/5] MQ ping-pong latency"
    for i in "${!PLACE_LABELS[@]}"; do
        label="${PLACE_LABELS[$i]}"
        cpus="${PLACE_CPUS[$i]}"
        for s in "${PINGPONG_SIZES_ARR[@]}"; do
            run_n_times "$csv" "$log" "${bindir}/pingpong" \
                "mq pingpong ${label} size=${s}B" \
                -n "$PP_ITERS" -s "$s" -c "$cpus" -p "$label"
        done
    done

    echo "[2/5] MQ throughput"
    for i in "${!PLACE_LABELS[@]}"; do
        label="${PLACE_LABELS[$i]}"
        cpus="${PLACE_CPUS[$i]}"
        for depth in "${THROUGHPUT_DEPTHS_ARR[@]}"; do
            for s in "${THROUGHPUT_SIZES_ARR[@]}"; do
                run_n_times "$csv" "$log" "${bindir}/throughput" \
                    "mq throughput ${label} depth=${depth} size=${s}B" \
                    -n "$TP_MSGS" -s "$s" -q "$depth" -c "$cpus" -p "${label}_q${depth}"
            done
        done
    done

    echo "[3/5] MQ scalability fan-in (c1)"
    for n in "${FANIN_N_ARR[@]}"; do
        for s in "${SCALABILITY_SIZES_ARR[@]}"; do
            run_n_times "$csv" "$log" "${bindir}/scalability" \
                "mq fanin n=${n} size=${s}B" \
                -m c1 -n "$n" -k "$SC_K" -s "$s" -p "fanin_n${n}"
        done
    done

    echo "[4/5] MQ scalability parallel pairs (c2)"
    for n in "${PAIRS_N_ARR[@]}"; do
        for s in "${SCALABILITY_SIZES_ARR[@]}"; do
            run_n_times "$csv" "$log" "${bindir}/scalability" \
                "mq pairs n=${n} size=${s}B" \
                -m c2 -n "$n" -k "$SC_K" -s "$s" -p "pairs_n${n}"
        done
    done

    echo "[5/5] SHM ping-pong baseline"
    for i in "${!PLACE_LABELS[@]}"; do
        label="${PLACE_LABELS[$i]}"
        cpus="${PLACE_CPUS[$i]}"
        for s in "${PINGPONG_SIZES_ARR[@]}"; do
            run_n_times "$csv" "$log" "${bindir}/shm_pingpong" \
                "shm pingpong ${label} size=${s}B" \
                -n "$PP_ITERS" -s "$s" -c "$cpus" -p "$label"
        done
    done

    rows=$(( $(wc -l < "$csv") - 1 ))
    echo "[${stack}] complete -> ${csv} (${rows} data rows)"
    echo "[${stack}] log      -> ${log}"
}

if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "posix" ]]; then
    run_stack posix
fi

if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "sysv" ]]; then
    run_stack sysv
fi

echo ""
echo "Combining POSIX + SysV CSVs"
bash "${ROOT_DIR}/scripts/combine_results.sh"

echo ""
echo "Done. Next step:"
echo "  python3 ${ROOT_DIR}/scripts/analyze.py"
