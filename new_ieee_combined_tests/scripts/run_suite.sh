#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────
# run_suite.sh — main benchmark driver
#
# Runs the full POSIX vs SysV IPC matrix across topology-aware placements
# and emits one CSV per stack.  Within a stack, all (config, run_id) tuples
# are shuffled before execution (RANDOMIZE=1, default) so system drift is
# decoupled from mechanism comparison.
#
# Tests:  MQ ping-pong, MQ throughput, MQ fan-in, MQ parallel pairs,
#         SHM ping-pong, SHM throughput, SHM parallel pairs.
# ──────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DEFAULT_POSIX_DIR="${ROOT_DIR}/stacks/POSIX"
DEFAULT_SYSV_DIR="${ROOT_DIR}/stacks/SYSV"

POSIX_DIR="${BENCH_POSIX_DIR:-${DEFAULT_POSIX_DIR}}"
SYSV_DIR="${BENCH_SYSV_DIR:-${DEFAULT_SYSV_DIR}}"
CONFIG_FILE="${ROOT_DIR}/config/defaults.env"

[[ -f "$CONFIG_FILE" ]] || { echo "Missing config: ${CONFIG_FILE}" >&2; exit 1; }
# shellcheck disable=SC1090
source "$CONFIG_FILE"

ONLY_STACK="both"
DO_BUILD=1
QUICK=0
DO_SETUP_SYSCTL=1
DO_SETUP_PERF=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only) shift; ONLY_STACK="$1" ;;
        --runs) shift; RUNS="$1" ;;
        --quick) QUICK=1 ;;
        --no-build) DO_BUILD=0 ;;
        --no-randomize) RANDOMIZE=0 ;;
        --skip-setup-sysctl) DO_SETUP_SYSCTL=0 ;;
        --setup-perf) DO_SETUP_PERF=1 ;;
        --help)
            cat <<EOF
Usage: bash scripts/run_suite.sh [options]

Options:
  --only {both|posix|sysv}   Run selected stack only (default: both)
  --runs N                   Repetitions per configuration
  --quick                    Fast smoke mode (small counts)
  --no-build                 Skip 'make all' in source stacks
  --no-randomize             Run configs in deterministic order
  --skip-setup-sysctl        Skip kernel IPC sysctl setup
  --setup-perf               Apply perf sysctl setup too
  --help                     This help
EOF
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
    shift
done

[[ "$ONLY_STACK" == "both" || "$ONLY_STACK" == "posix" || "$ONLY_STACK" == "sysv" ]] || {
    echo "Invalid --only value: ${ONLY_STACK}" >&2; exit 1; }

if [[ "$QUICK" -eq 1 ]]; then
    RUNS="$QUICK_RUNS"
    PP_ITERS="$QUICK_PP_ITERS"
    TP_MSGS="$QUICK_TP_MSGS"
    SC_K="$QUICK_SC_K"
fi

[[ "$RUNS" =~ ^[0-9]+$ && "$RUNS" -ge 1 ]] || { echo "RUNS must be a positive int." >&2; exit 1; }
[[ -d "$POSIX_DIR" ]] || { echo "POSIX dir not found: ${POSIX_DIR}" >&2; exit 1; }
[[ -d "$SYSV_DIR"  ]] || { echo "SYSV dir not found: ${SYSV_DIR}"  >&2; exit 1; }

mkdir -p "${ROOT_DIR}/results/raw/posix" \
         "${ROOT_DIR}/results/raw/sysv" \
         "${ROOT_DIR}/results/raw/combined" \
         "${ROOT_DIR}/results/logs"

# ── system setup ─────────────────────────────────────────────────────
SUDO_NONINTERACTIVE_OK=0
sudo -n true >/dev/null 2>&1 && SUDO_NONINTERACTIVE_OK=1

if [[ "$DO_SETUP_SYSCTL" -eq 1 ]]; then
    echo "Applying kernel IPC limits..."
    bash "${ROOT_DIR}/scripts/setup_sysctl.sh"
fi

if [[ "$DO_SETUP_PERF" -eq 1 ]]; then
    echo "Applying perf limits..."
    bash "${ROOT_DIR}/scripts/setup_perf.sh"
fi

TARGET_NOFILE="${BENCH_NOFILE_LIMIT:-65536}"
TARGET_MSGQUEUE_BYTES="${BENCH_MSGQUEUE_LIMIT:-67108864}"

raise_limit() {
    local name="$1" target="$2" verb="$3" probe="$4"
    if eval "$probe" "$target" 2>/dev/null; then return 0; fi
    if command -v prlimit >/dev/null 2>&1 && [[ "$SUDO_NONINTERACTIVE_OK" -eq 1 ]]; then
        if sudo -n prlimit --pid $$ --"$verb"="${target}:${target}" >/dev/null 2>&1 \
           && eval "$probe" "$target" 2>/dev/null; then
            echo "Raised $name via sudo prlimit."
            return 0
        fi
    fi
    echo "Warning: could not raise $name to ${target} (current: $(eval $probe))." >&2
}
raise_limit "RLIMIT_NOFILE"   "$TARGET_NOFILE"         "nofile"   "ulimit -n"
raise_limit "RLIMIT_MSGQUEUE" "$TARGET_MSGQUEUE_BYTES" "msgqueue" "ulimit -q"
echo "Effective limits: nofile=$(ulimit -n), msgqueue=$(ulimit -q)"

# CPU governor
if [ -d "/sys/devices/system/cpu/cpu0/cpufreq" ]; then
    if [[ "$SUDO_NONINTERACTIVE_OK" -eq 1 ]]; then
        echo performance | sudo -n tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null \
            && echo "CPU governor: performance" || echo "Warning: failed to set governor." >&2
    else
        echo "Warning: sudo creds unavailable; skipping governor." >&2
    fi
    gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)
    echo "Current governor: $gov"
fi

# Topology probe
eval "$("${ROOT_DIR}/scripts/detect_cpu_pairs.sh" --export)"

TOPO_LOG="${ROOT_DIR}/results/logs/topology.txt"
{
    echo "Generated: $(date -Iseconds)"
    echo ""
    echo "=== System ==="
    uname -a || true
    echo ""
    echo "=== CPU ==="
    if command -v lscpu >/dev/null 2>&1; then lscpu || true; fi
    echo ""
    echo "=== Compiler ==="
    gcc --version 2>/dev/null | head -1 || true
    echo ""
    echo "=== Mitigations ==="
    if compgen -G "/sys/devices/system/cpu/vulnerabilities/*" >/dev/null; then
        for f in /sys/devices/system/cpu/vulnerabilities/*; do
            if [[ -f "$f" ]]; then
                printf "%-30s %s\n" "$(basename "$f")" "$(cat "$f" 2>/dev/null || echo '?')"
            fi
        done
    fi
    echo ""
    echo "=== Boost / Governor ==="
    if [[ -f /sys/devices/system/cpu/cpufreq/boost ]]; then
        echo "cpufreq boost: $(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null || echo '?')"
    fi
    if [[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
        echo "intel_pstate no_turbo: $(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo '?')"
    fi
    echo "governor: ${gov:-unknown}"
    echo ""
    echo "=== Placement pairs ==="
    "${ROOT_DIR}/scripts/detect_cpu_pairs.sh" --summary || true
    echo ""
    echo "=== Run config ==="
    echo "RUNS=${RUNS}  PP_ITERS=${PP_ITERS}  TP_MSGS=${TP_MSGS}  SC_K=${SC_K}"
    echo "RANDOMIZE=${RANDOMIZE}"
} > "$TOPO_LOG" 2>&1 || true

# ── arrays ───────────────────────────────────────────────────────────
read -r -a PINGPONG_SIZES_ARR    <<< "$PINGPONG_SIZES"
read -r -a THROUGHPUT_SIZES_ARR  <<< "$THROUGHPUT_SIZES"
read -r -a THROUGHPUT_DEPTHS_ARR <<< "$THROUGHPUT_DEPTHS"
read -r -a SCALABILITY_SIZES_ARR <<< "$SCALABILITY_SIZES"
read -r -a FANIN_N_ARR           <<< "$FANIN_N"
read -r -a PAIRS_N_ARR           <<< "$PAIRS_N"

# Build the placement list dynamically from whichever axes the topology
# script reports.  Empty axes are skipped automatically.
PLACE_LABELS=()
PLACE_CPUS=()
add_place() {
    local label="$1" cpus="$2"
    if [[ -n "$cpus" ]]; then
        PLACE_LABELS+=("$label")
        PLACE_CPUS+=("$cpus")
    fi
    return 0
}
add_place "same_core" "${SAME_CORE_CPUS:-}"
add_place "same_l2"   "${SAME_L2_CPUS:-}"
add_place "same_l3"   "${SAME_L3_CPUS:-}"
add_place "diff_l3"   "${DIFF_L3_CPUS:-}"

if [[ ${#PLACE_LABELS[@]} -eq 0 ]]; then
    echo "No placement pairs detected. Aborting." >&2
    exit 1
fi
echo "Active placements: ${PLACE_LABELS[*]}"

# ── card builder ─────────────────────────────────────────────────────
# Each card is a TAB-separated line:
#   csv \t log \t desc \t bin_path \t args
# args is space-separated and must not contain tabs.

build_cards() {
    local stack="$1" bindir="$2" csv="$3" log="$4"

    # MQ ping-pong: per placement, per size
    for i in "${!PLACE_LABELS[@]}"; do
        local label="${PLACE_LABELS[$i]}" cpus="${PLACE_CPUS[$i]}"
        for s in "${PINGPONG_SIZES_ARR[@]}"; do
            for ((r=1; r<=RUNS; r++)); do
                printf '%s\t%s\tmq pingpong %s s=%dB r=%d\t%s\t-r %d -n %d -s %d -c %s -p %s\n' \
                    "$csv" "$log" "$label" "$s" "$r" \
                    "${bindir}/pingpong" \
                    "$r" "$PP_ITERS" "$s" "$cpus" "$label"
            done
        done
    done

    # MQ throughput: per placement, per depth, per size
    for i in "${!PLACE_LABELS[@]}"; do
        local label="${PLACE_LABELS[$i]}" cpus="${PLACE_CPUS[$i]}"
        for depth in "${THROUGHPUT_DEPTHS_ARR[@]}"; do
            for s in "${THROUGHPUT_SIZES_ARR[@]}"; do
                for ((r=1; r<=RUNS; r++)); do
                    printf '%s\t%s\tmq throughput %s q=%d s=%dB r=%d\t%s\t-r %d -n %d -s %d -q %d -c %s -p %s_q%d\n' \
                        "$csv" "$log" "$label" "$depth" "$s" "$r" \
                        "${bindir}/throughput" \
                        "$r" "$TP_MSGS" "$s" "$depth" "$cpus" "$label" "$depth"
                done
            done
        done
    done

    # MQ scalability fan-in: per N, per size (no placement)
    for n in "${FANIN_N_ARR[@]}"; do
        for s in "${SCALABILITY_SIZES_ARR[@]}"; do
            for ((r=1; r<=RUNS; r++)); do
                printf '%s\t%s\tmq fanin n=%d s=%dB r=%d\t%s\t-r %d -m c1 -n %d -k %d -s %d -p fanin_n%d\n' \
                    "$csv" "$log" "$n" "$s" "$r" \
                    "${bindir}/scalability" \
                    "$r" "$n" "$SC_K" "$s" "$n"
            done
        done
    done

    # MQ scalability parallel pairs
    for n in "${PAIRS_N_ARR[@]}"; do
        for s in "${SCALABILITY_SIZES_ARR[@]}"; do
            for ((r=1; r<=RUNS; r++)); do
                printf '%s\t%s\tmq pairs n=%d s=%dB r=%d\t%s\t-r %d -m c2 -n %d -k %d -s %d -p pairs_n%d\n' \
                    "$csv" "$log" "$n" "$s" "$r" \
                    "${bindir}/scalability" \
                    "$r" "$n" "$SC_K" "$s" "$n"
            done
        done
    done

    # SHM ping-pong: per placement, per size
    for i in "${!PLACE_LABELS[@]}"; do
        local label="${PLACE_LABELS[$i]}" cpus="${PLACE_CPUS[$i]}"
        for s in "${PINGPONG_SIZES_ARR[@]}"; do
            for ((r=1; r<=RUNS; r++)); do
                printf '%s\t%s\tshm pingpong %s s=%dB r=%d\t%s\t-r %d -n %d -s %d -c %s -p %s\n' \
                    "$csv" "$log" "$label" "$s" "$r" \
                    "${bindir}/shm_pingpong" \
                    "$r" "$PP_ITERS" "$s" "$cpus" "$label"
            done
        done
    done

    # SHM throughput
    for i in "${!PLACE_LABELS[@]}"; do
        local label="${PLACE_LABELS[$i]}" cpus="${PLACE_CPUS[$i]}"
        for depth in "${THROUGHPUT_DEPTHS_ARR[@]}"; do
            for s in "${THROUGHPUT_SIZES_ARR[@]}"; do
                for ((r=1; r<=RUNS; r++)); do
                    printf '%s\t%s\tshm throughput %s q=%d s=%dB r=%d\t%s\t-r %d -n %d -s %d -q %d -c %s -p %s_q%d\n' \
                        "$csv" "$log" "$label" "$depth" "$s" "$r" \
                        "${bindir}/shm_throughput" \
                        "$r" "$TP_MSGS" "$s" "$depth" "$cpus" "$label" "$depth"
                done
            done
        done
    done

    # SHM parallel pairs (no placement; binary uses round-robin pinning)
    for n in "${PAIRS_N_ARR[@]}"; do
        for s in "${SCALABILITY_SIZES_ARR[@]}"; do
            for ((r=1; r<=RUNS; r++)); do
                printf '%s\t%s\tshm pairs n=%d s=%dB r=%d\t%s\t-r %d -n %d -k %d -s %d -p pairs_n%d\n' \
                    "$csv" "$log" "$n" "$s" "$r" \
                    "${bindir}/shm_pairs" \
                    "$r" "$n" "$SC_K" "$s" "$n"
            done
        done
    done
}

# ── per-stack runner ─────────────────────────────────────────────────
run_stack() {
    local stack="$1"
    local stack_dir
    [[ "$stack" == "posix" ]] && stack_dir="$POSIX_DIR" || stack_dir="$SYSV_DIR"
    local bindir="${stack_dir}/bin"
    local csv="${ROOT_DIR}/results/raw/${stack}/all_runs.csv"
    local log="${ROOT_DIR}/results/logs/${stack}_run.log"
    : > "$log"

    if [[ "$DO_BUILD" -eq 1 ]]; then
        echo "[${stack}] Building binaries"
        make -C "$stack_dir" all >> "$log" 2>&1
    fi

    for b in pingpong throughput scalability shm_pingpong shm_throughput shm_pairs; do
        [[ -x "${bindir}/${b}" ]] || {
            echo "Missing binary: ${bindir}/${b}" >&2
            echo "Build log: ${log}" >&2
            exit 1
        }
    done

    # CSV header (uses pingpong's -H)
    "${bindir}/pingpong" -H > "$csv"

    local cards_file
    cards_file=$(mktemp)
    build_cards "$stack" "$bindir" "$csv" "$log" > "$cards_file"

    local total
    total=$(wc -l < "$cards_file")
    echo ""
    echo "============================================================"
    echo "[${stack}] Total invocations: ${total}  RANDOMIZE=${RANDOMIZE}"
    echo "============================================================"

    if [[ "$RANDOMIZE" -eq 1 ]]; then
        local shuffled
        shuffled=$(mktemp)
        shuf "$cards_file" > "$shuffled"
        mv "$shuffled" "$cards_file"
    fi

    local i=0
    while IFS=$'\t' read -r out_csv out_log desc bin args; do
        i=$((i + 1))
        printf "[%d/%d] %s\n" "$i" "$total" "$desc"
        # shellcheck disable=SC2086
        "$bin" $args >> "$out_csv" 2>> "$out_log" || {
            echo "  FAILED: $bin $args" | tee -a "$out_log" >&2
        }
    done < "$cards_file"
    rm -f "$cards_file"

    local rows=$(( $(wc -l < "$csv") - 1 ))
    echo "[${stack}] complete -> ${csv} (${rows} data rows)"
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
echo "Done. Next:"
echo "  python3 ${ROOT_DIR}/scripts/analyze.py"
