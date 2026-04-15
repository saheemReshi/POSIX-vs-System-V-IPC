#!/usr/bin/env bash
# =============================================================================
# perf_wrap.sh — Hardware performance counter profiling for IPC benchmarks
#
# Wraps each benchmark with `perf stat` to collect:
#   - cycles, instructions  → IPC (instructions per cycle)
#   - cache-references, cache-misses → cache miss rate
#   - dTLB-load-misses, dTLB-loads  → TLB miss rate (separate from cache)
#   - context-switches, cpu-migrations
#
# Why separate TLB from cache?
#   POSIX MQ's mq_send/mq_receive perform kernel-to-user copies.  Each copy
#   page-walks a different address space (kernel queue buffer → user buffer),
#   generating dTLB misses independently of L1/L2 cache pressure.  Systems
#   that bypass the kernel (shm_posix) avoid this copy and show dramatically
#   lower dTLB miss rates.  This is a key mechanistic result for the paper.
#
# Output: results/perf_stat.csv
#   Columns: mechanism, benchmark, msg_size_bytes, placement,
#             cycles, instructions, ipc,
#             cache_misses, cache_refs, cache_miss_rate,
#             dtlb_load_misses, dtlb_loads, dtlb_miss_rate,
#             context_switches, migrations
#
# Usage:
#   bash analysis/perf_wrap.sh [--quick] [--help]
#
#   --quick   reduced iteration counts for fast testing
#   --help    print this help
#
# Prerequisites:
#   make all
#   make setup_sysctl
#   make setup_perf_sysctl   (sets kernel.perf_event_paranoid=-1)
#   perf must be installed:  sudo apt-get install linux-tools-common
# =============================================================================

set -euo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────
QUICK=0
PP_ITERS=50000
TP_MSGS=100000
SC_K=5000

RESDIR="results"
PERF_DIR="${RESDIR}/perf"
OUT_CSV="${RESDIR}/perf_stat.csv"

PP="bin/pingpong"
TP="bin/throughput"
SC="bin/scalability"
SHM="bin/shm_pingpong"

# ── Argument parsing ─────────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=1; PP_ITERS=5000; TP_MSGS=10000; SC_K=500 ;;
        --help)
            sed -n '3,35p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
    esac
done

# ── Preflight ────────────────────────────────────────────────────────────────
if ! command -v perf &>/dev/null; then
    echo "ERROR: perf not found. Install with:" >&2
    echo "  sudo apt-get install linux-tools-common linux-tools-\$(uname -r)" >&2
    exit 1
fi

for bin in "$PP" "$TP" "$SC" "$SHM"; do
    if [[ ! -x "$bin" ]]; then
        echo "ERROR: $bin not found. Run 'make all' first." >&2
        exit 1
    fi
done

mkdir -p "$PERF_DIR"

# Check perf_event_paranoid
paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 2)
if [[ "$paranoid" -gt 0 ]]; then
    echo "WARNING: kernel.perf_event_paranoid=$paranoid — some hardware" >&2
    echo "  counters may be unavailable. Run 'make setup_perf_sysctl'." >&2
fi

# ── perf event string ─────────────────────────────────────────────────────────
# We request both cache and TLB events separately.
# dTLB events may not be available on all CPUs; perf will report <not counted>
# which we handle in the parsing step.
EVENTS="cycles,instructions,cache-references,cache-misses"
EVENTS+=",dTLB-load-misses,dTLB-loads"
EVENTS+=",context-switches,cpu-migrations"

# ── Parsing helper ────────────────────────────────────────────────────────────
# perf stat -x, outputs:  value,unit,event,run%,variance
# We parse each event by name and extract the numeric value.

parse_perf_output() {
    local perf_file="$1"
    local -A vals

    # Initialise to 0
    vals[cycles]=0; vals[instructions]=0
    vals[cache-references]=0; vals[cache-misses]=0
    vals[dTLB-load-misses]=0; vals[dTLB-loads]=0
    vals[context-switches]=0; vals[cpu-migrations]=0

    while IFS=',' read -r value unit event rest; do
        # Skip comment lines and <not counted>
        [[ "$value" =~ ^# ]] && continue
        [[ "$value" == "<not counted>" ]] && continue
        [[ "$value" == "<not supported>" ]] && continue
        # Remove leading/trailing whitespace
        value="${value//[[:space:]]/}"
        event="${event//[[:space:]]/}"
        # Remove commas in large numbers (e.g. 1,234,567 → 1234567)
        value="${value//,/}"
        if [[ -n "${vals[$event]+x}" ]]; then
            vals[$event]="$value"
        fi
    done < "$perf_file"

    local cycles="${vals[cycles]}"
    local instr="${vals[instructions]}"
    local cache_ref="${vals[cache-references]}"
    local cache_miss="${vals[cache-misses]}"
    local dtlb_miss="${vals[dTLB-load-misses]}"
    local dtlb_load="${vals[dTLB-loads]}"
    local ctx="${vals[context-switches]}"
    local mig="${vals[cpu-migrations]}"

    # Compute derived metrics (use awk for floating point)
    local ipc cache_rate dtlb_rate
    ipc=$(awk "BEGIN { printf \"%.4f\", ($instr>0 && $cycles>0) ? $instr/$cycles : 0 }")
    cache_rate=$(awk "BEGIN { printf \"%.6f\", ($cache_ref>0) ? $cache_miss/$cache_ref : 0 }")
    dtlb_rate=$(awk "BEGIN { printf \"%.6f\", ($dtlb_load>0) ? $dtlb_miss/$dtlb_load : 0 }")

    echo "$cycles,$instr,$ipc,$cache_miss,$cache_ref,$cache_rate,$dtlb_miss,$dtlb_load,$dtlb_rate,$ctx,$mig"
}

# ── Run one benchmark under perf stat ────────────────────────────────────────
run_perf() {
    local mechanism="$1"; shift
    local benchmark="$1";  shift
    local msg_size="$1";   shift
    local placement="$1";  shift
    # remaining args: the full benchmark command

    local perf_raw="${PERF_DIR}/${mechanism}_${benchmark}_${msg_size}_${placement}.txt"

    printf "  %-55s " "${mechanism}/${benchmark} ${msg_size}B ${placement} ..."

    # Run perf stat with CSV-format output (-x,)
    perf stat -e "$EVENTS" -x, -o "$perf_raw" -- "$@" \
        >/dev/null 2>>"$perf_raw" || true

    local parsed
    parsed=$(parse_perf_output "$perf_raw")
    echo "${mechanism},${benchmark},${msg_size},${placement},${parsed}" >> "$OUT_CSV"
    echo "done"
}

# ── Write CSV header ──────────────────────────────────────────────────────────
cat > "$OUT_CSV" << 'EOF'
mechanism,benchmark,msg_size_bytes,placement,cycles,instructions,ipc,cache_misses,cache_refs,cache_miss_rate,dtlb_load_misses,dtlb_loads,dtlb_miss_rate,context_switches,migrations
EOF

echo "============================================================"
echo " perf_wrap.sh — IPC/TLB hardware counter profiling"
echo "============================================================"
echo " Events: $EVENTS"
echo " Output: $OUT_CSV"
echo "============================================================"
echo ""

# ── Representative configurations ────────────────────────────────────────────
# We profile a representative subset rather than the full sweep:
#   - Small (64B) and large (8192B) messages → shows copy-cost inflection
#   - same_socket and cross_socket placements → shows memory hierarchy cost
#   - Depth=64 for throughput (peak BW mode)
#   - Fan-in n=1 vs n=8 → shows lock-contention impact on IPC

# Detect a cross-socket pair, fallback to 0,1
CPU_PAIR="0,1"
if [[ -f "results/topo.txt" ]]; then
    cs_line=$(grep "cross_socket" results/topo.txt 2>/dev/null | head -1 || true)
    if [[ -n "$cs_line" ]]; then
        CPU_PAIR=$(echo "$cs_line" | grep -oP 'CPUs \K[0-9]+,[0-9]+' || echo "0,1")
    fi
fi
echo "  Using CPU pair: $CPU_PAIR"

# ── Ping-Pong: POSIX MQ ───────────────────────────────────────────────────────
echo "[1/4] Ping-Pong — POSIX MQ"
for sz in 64 1024 8192; do
    run_perf "mq_posix" "pingpong" "$sz" "same_socket" \
        "$PP" -n "$PP_ITERS" -s "$sz" -c 0,1 -p same_socket
    run_perf "mq_posix" "pingpong" "$sz" "cross_socket" \
        "$PP" -n "$PP_ITERS" -s "$sz" -c "$CPU_PAIR" -p cross_socket
done

# ── Ping-Pong: Shared Memory ──────────────────────────────────────────────────
echo ""
echo "[2/4] Ping-Pong — Shared Memory"
for sz in 64 1024 8192; do
    run_perf "shm_posix" "pingpong" "$sz" "same_socket" \
        "$SHM" -n "$PP_ITERS" -s "$sz" -c 0,1 -p same_socket
    run_perf "shm_posix" "pingpong" "$sz" "cross_socket" \
        "$SHM" -n "$PP_ITERS" -s "$sz" -c "$CPU_PAIR" -p cross_socket
done

# ── Throughput: POSIX MQ ──────────────────────────────────────────────────────
echo ""
echo "[3/4] Throughput — POSIX MQ (depth=64)"
for sz in 64 1024 8192; do
    run_perf "mq_posix" "throughput" "$sz" "cross_socket" \
        "$TP" -n "$TP_MSGS" -s "$sz" -q 64 -c "$CPU_PAIR" -p cross_socket
done

# ── Scalability: Fan-in ───────────────────────────────────────────────────────
echo ""
echo "[4/4] Fan-in scalability — POSIX MQ"
for n in 1 4 8; do
    run_perf "mq_posix" "fanin_n${n}" "64" "fanin" \
        "$SC" -m c1 -n "$n" -k "$SC_K" -s 64 -p fanin
done

echo ""
echo "============================================================"
echo " perf profiling complete."
echo " Output: $OUT_CSV"
echo " Raw perf files: $PERF_DIR/"
echo ""
echo " Next step:"
echo "   python3 analysis/analyze.py --perf $OUT_CSV"
echo "============================================================"
