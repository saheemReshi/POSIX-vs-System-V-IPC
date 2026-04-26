#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────
# run_perf.sh – Collect perf-stat hardware counters for every benchmark
#               configuration (same matrix as run_suite.sh).
#
# Output:  results/perf/posix/all_perf.csv
#          results/perf/sysv/all_perf.csv
# Format:  CSV with columns:
#   mechanism,benchmark,placement,msg_size,queue_depth,n_procs,
#   run_id,event,value
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

# ── perf availability ────────────────────────────────────────────────
if ! command -v perf &>/dev/null; then
    echo "ERROR: 'perf' is not installed or not in PATH." >&2
    echo "Install it with:  sudo apt install linux-tools-common linux-tools-\$(uname -r)" >&2
    exit 1
fi

# ── CLI defaults ─────────────────────────────────────────────────────
ONLY_STACK="both"      # both | posix | sysv
DO_BUILD=1
QUICK=0
DO_SETUP_SYSCTL=1
DO_SETUP_PERF=1         # default ON for perf script (needs perf_event_paranoid=-1)

# Allow env-level override; fall back to config's PERF_RUNS (or 3).
PERF_RUNS="${PERF_RUNS:-${PERF_RUNS:-3}}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stack|--only)
            shift; ONLY_STACK="$1" ;;
        --runs)
            shift; PERF_RUNS="$1" ;;
        --quick)
            QUICK=1 ;;
        --no-build)
            DO_BUILD=0 ;;
        --skip-setup-sysctl)
            DO_SETUP_SYSCTL=0 ;;
        --skip-setup-perf)
            DO_SETUP_PERF=0 ;;
        --help)
            cat <<EOF
Usage: bash scripts/run_perf.sh [options]

Collects perf-stat counters for every benchmark in the test matrix
(same configurations as run_suite.sh).

Results are stored per-stack:
  results/perf/posix/
  results/perf/sysv/

Options:
  --stack {both|posix|sysv}  Run selected stack only (default: both)
  --runs  N                  Repetitions per config (default: PERF_RUNS from env / 3)
  --quick                    Fast smoke mode (small iteration counts)
  --no-build                 Skip 'make all' in source stacks
  --skip-setup-sysctl        Skip kernel IPC sysctl setup
  --skip-setup-perf          Skip perf sysctl setup (perf_event_paranoid)
  --help                     Show this help

Environment overrides:
  PERF_RUNS=N                Same as --runs (CLI takes priority)

Perf events collected:
  task-clock, cycles, instructions, cache-references, cache-misses,
  minor-faults, major-faults, context-switches, cpu-migrations
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

if ! [[ "$PERF_RUNS" =~ ^[0-9]+$ ]] || [[ "$PERF_RUNS" -lt 1 ]]; then
    echo "PERF_RUNS must be a positive integer." >&2
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

if [[ "$DO_SETUP_PERF" -eq 1 ]]; then
    echo "Applying perf limits (setup_perf)..."
    bash "${ROOT_DIR}/scripts/setup_perf.sh"
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

# ── perf events ──────────────────────────────────────────────────────
PERF_EVENTS="task-clock,cycles,instructions,cache-references,cache-misses,minor-faults,major-faults,context-switches,cpu-migrations"

# ── CPU topology ─────────────────────────────────────────────────────
eval "$("${ROOT_DIR}/scripts/detect_cpu_pairs.sh" --export)"

# ── arrays from config ───────────────────────────────────────────────
read -r -a PINGPONG_SIZES_ARR   <<< "$PINGPONG_SIZES"
read -r -a THROUGHPUT_SIZES_ARR <<< "$THROUGHPUT_SIZES"
read -r -a THROUGHPUT_DEPTHS_ARR<<< "$THROUGHPUT_DEPTHS"
read -r -a SCALABILITY_SIZES_ARR<<< "$SCALABILITY_SIZES"
read -r -a FANIN_N_ARR          <<< "$FANIN_N"
read -r -a PAIRS_N_ARR          <<< "$PAIRS_N"

PLACE_LABELS=("same_core" "cross_core")
PLACE_CPUS=("$SAME_CORE_CPUS" "$CROSS_CORE_CPUS")

TOTAL_RUNS=0

# ── CSV header ───────────────────────────────────────────────────────
CSV_HEADER="mechanism,benchmark,placement,msg_size,queue_depth,n_procs,run_id,event,value"

# ── helpers ──────────────────────────────────────────────────────────

# perf_run <csv> <mechanism> <benchmark> <placement> <msg_size> \
#          <queue_depth> <n_procs> <description> <binary> [binary args...]
#
# Runs `perf stat` PERF_RUNS times, appending rows to the single CSV.
perf_run() {
    local csv="$1"; shift
    local mechanism="$1"; shift
    local benchmark="$1"; shift
    local placement="$1"; shift
    local msg_size="$1"; shift
    local queue_depth="$1"; shift
    local n_procs="$1"; shift
    local desc="$1"; shift
    local bin="$1"; shift

    printf "    %-62s " "$desc ..."

    local perf_tmp
    perf_tmp=$(mktemp "${ROOT_DIR}/results/perf/.perf_tmp.XXXXXX")

    for ((r=1; r<=PERF_RUNS; r++)); do
        # perf stat writes counters to stderr in CSV mode (-x,).
        # Benchmark stdout/stderr goes to the log; perf stderr is captured.
        perf stat -x, \
            -e "$PERF_EVENTS" \
            -- "$bin" -r "$r" "$@" \
            2>"$perf_tmp" || true

        # Parse perf CSV output with awk.
        # perf -x, format: value,,event-name,run,pct,,
        # Field 1 = value (counter), Field 3 = event name.
        # Skip blank lines, comments (#), and <not counted> / <not supported>.
        awk -F',' '
            /^[[:space:]]*$/ { next }
            /^#/             { next }
            /<not counted>/  { next }
            /<not supported>/{ next }
            {
                val  = $1
                evt  = $3
                if (evt == "") next
                if (val == "") next
                printf "%s,%s,%s,%s,%s,%s,%d,%s,%s\n", \
                    mech, bench, place, msz, qdepth, nprocs, run, evt, val
            }
        ' mech="$mechanism" bench="$benchmark" place="$placement" \
          msz="$msg_size" qdepth="$queue_depth" nprocs="$n_procs" \
          run="$r" "$perf_tmp" >> "$csv"

        TOTAL_RUNS=$((TOTAL_RUNS + 1))
    done

    rm -f "$perf_tmp"
    echo "done (${PERF_RUNS} runs)"
}

# ── per-stack driver ─────────────────────────────────────────────────
run_stack_perf() {
    local stack="$1"
    local stack_dir=""

    if [[ "$stack" == "posix" ]]; then
        stack_dir="$POSIX_DIR"
    else
        stack_dir="$SYSV_DIR"
    fi

    local bindir="${stack_dir}/bin"
    local outdir="${ROOT_DIR}/results/perf/${stack}"
    local csv="${outdir}/all_perf.csv"
    local log="${ROOT_DIR}/results/logs/${stack}_perf.log"

    mkdir -p "$outdir" "${ROOT_DIR}/results/logs"
    : > "$log"

    # Write CSV header (truncate any previous run)
    echo "$CSV_HEADER" > "$csv"

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
    echo "[${stack}] Collecting perf-stat counters"
    echo "============================================================"

    # ── [1/5] MQ ping-pong latency ──────────────────────────────────
    echo "[1/5] MQ ping-pong latency"
    for i in "${!PLACE_LABELS[@]}"; do
        label="${PLACE_LABELS[$i]}"
        cpus="${PLACE_CPUS[$i]}"
        for s in "${PINGPONG_SIZES_ARR[@]}"; do
            perf_run "$csv" "$stack" "pingpong" "$label" "$s" "" "" \
                "mq pingpong ${label} size=${s}B" \
                "${bindir}/pingpong" \
                -n "$PP_ITERS" -s "$s" -c "$cpus" -p "$label"
        done
    done

    # ── [2/5] MQ throughput ──────────────────────────────────────────
    echo "[2/5] MQ throughput"
    for i in "${!PLACE_LABELS[@]}"; do
        label="${PLACE_LABELS[$i]}"
        cpus="${PLACE_CPUS[$i]}"
        for depth in "${THROUGHPUT_DEPTHS_ARR[@]}"; do
            for s in "${THROUGHPUT_SIZES_ARR[@]}"; do
                perf_run "$csv" "$stack" "throughput" "$label" "$s" "$depth" "" \
                    "mq throughput ${label} depth=${depth} size=${s}B" \
                    "${bindir}/throughput" \
                    -n "$TP_MSGS" -s "$s" -q "$depth" -c "$cpus" -p "${label}_q${depth}"
            done
        done
    done

    # ── [3/5] MQ scalability fan-in ──────────────────────────────────
    echo "[3/5] MQ scalability fan-in"
    for n in "${FANIN_N_ARR[@]}"; do
        for s in "${SCALABILITY_SIZES_ARR[@]}"; do
            perf_run "$csv" "$stack" "scalability_fanin" "" "$s" "" "$n" \
                "mq fanin n=${n} size=${s}B" \
                "${bindir}/scalability" \
                -m c1 -n "$n" -k "$SC_K" -s "$s" -p "fanin_n${n}"
        done
    done

    # ── [4/5] MQ scalability pairs ───────────────────────────────────
    echo "[4/5] MQ scalability parallel pairs"
    for n in "${PAIRS_N_ARR[@]}"; do
        for s in "${SCALABILITY_SIZES_ARR[@]}"; do
            perf_run "$csv" "$stack" "scalability_pairs" "" "$s" "" "$n" \
                "mq pairs n=${n} size=${s}B" \
                "${bindir}/scalability" \
                -m c2 -n "$n" -k "$SC_K" -s "$s" -p "pairs_n${n}"
        done
    done

    # ── [5/5] SHM ping-pong baseline ─────────────────────────────────
    echo "[5/5] SHM ping-pong baseline"
    for i in "${!PLACE_LABELS[@]}"; do
        label="${PLACE_LABELS[$i]}"
        cpus="${PLACE_CPUS[$i]}"
        for s in "${PINGPONG_SIZES_ARR[@]}"; do
            perf_run "$csv" "$stack" "shm_pingpong" "$label" "$s" "" "" \
                "shm pingpong ${label} size=${s}B" \
                "${bindir}/shm_pingpong" \
                -n "$PP_ITERS" -s "$s" -c "$cpus" -p "$label"
        done
    done

    local rows
    rows=$(( $(wc -l < "$csv") - 1 ))
    echo "[${stack}] complete -> ${csv} (${rows} data rows)"
    echo "[${stack}] log      -> ${log}"
}

# ── main ─────────────────────────────────────────────────────────────
if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "posix" ]]; then
    run_stack_perf posix
fi

if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "sysv" ]]; then
    run_stack_perf sysv
fi

echo ""
echo "════════════════════════════════════════════════════════════════"
echo "  PERF COLLECTION SUMMARY"
echo "════════════════════════════════════════════════════════════════"
echo "  Total benchmark invocations : ${TOTAL_RUNS}"
echo "  Runs per configuration      : ${PERF_RUNS}"
echo "  Output files                :"
if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "posix" ]]; then
    echo "    results/perf/posix/all_perf.csv"
fi
if [[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "sysv" ]]; then
    echo "    results/perf/sysv/all_perf.csv"
fi
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "Done.  Load the CSV(s) directly for analysis (e.g. pandas.read_csv)."
