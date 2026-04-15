#!/usr/bin/env bash
# =============================================================================
# perf_wrap.sh — Hardware performance counter profiling (System V IPC)
#
# Collects, via perf stat:
#   - cycles, instructions -> IPC
#   - cache-references, cache-misses -> cache miss rate
#   - dTLB-load-misses, dTLB-loads -> dTLB miss rate
#   - context-switches, cpu-migrations
#
# Output: results/perf_stat.csv
# =============================================================================

set -euo pipefail

PP_ITERS=50000
TP_MSGS=100000
SC_K=5000

for arg in "$@"; do
    case "$arg" in
        --quick) PP_ITERS=5000; TP_MSGS=10000; SC_K=500 ;;
        --help)
            sed -n '3,20p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
    esac
done

RESDIR="results"
PERF_DIR="${RESDIR}/perf"
OUT_CSV="${RESDIR}/perf_stat.csv"

PP="bin/pingpong"
TP="bin/throughput"
SC="bin/scalability"
SHM="bin/shm_pingpong"

if ! command -v perf &>/dev/null; then
    echo "ERROR: perf not found. Install linux-tools for your kernel." >&2
    exit 1
fi

for bin in "$PP" "$TP" "$SC" "$SHM"; do
    [[ -x "$bin" ]] || { echo "ERROR: $bin not found. Run 'make all' first." >&2; exit 1; }
done

mkdir -p "$PERF_DIR"

paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 2)
if [[ "$paranoid" -gt 0 ]]; then
    echo "WARNING: kernel.perf_event_paranoid=$paranoid; some events may be unavailable." >&2
fi

EVENTS="cycles,instructions,cache-references,cache-misses"
EVENTS+=",dTLB-load-misses,dTLB-loads"
EVENTS+=",context-switches,cpu-migrations"

parse_perf_output() {
    local perf_file="$1"
    local -A vals

    vals[cycles]=0; vals[instructions]=0
    vals[cache-references]=0; vals[cache-misses]=0
    vals[dTLB-load-misses]=0; vals[dTLB-loads]=0
    vals[context-switches]=0; vals[cpu-migrations]=0

    while IFS=',' read -r value _unit event _rest; do
        [[ "$value" =~ ^# ]] && continue
        [[ "$value" == "<not counted>" ]] && continue
        [[ "$value" == "<not supported>" ]] && continue
        value="${value//[[:space:]]/}"
        event="${event//[[:space:]]/}"
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

    local ipc cache_rate dtlb_rate
    ipc=$(awk "BEGIN { printf \"%.4f\", ($instr>0 && $cycles>0) ? $instr/$cycles : 0 }")
    cache_rate=$(awk "BEGIN { printf \"%.6f\", ($cache_ref>0) ? $cache_miss/$cache_ref : 0 }")
    dtlb_rate=$(awk "BEGIN { printf \"%.6f\", ($dtlb_load>0) ? $dtlb_miss/$dtlb_load : 0 }")

    echo "$cycles,$instr,$ipc,$cache_miss,$cache_ref,$cache_rate,$dtlb_miss,$dtlb_load,$dtlb_rate,$ctx,$mig"
}

run_perf() {
    local mechanism="$1"; shift
    local benchmark="$1"; shift
    local msg_size="$1"; shift
    local placement="$1"; shift

    local perf_raw="${PERF_DIR}/${mechanism}_${benchmark}_${msg_size}_${placement}.txt"
    printf "  %-55s " "${mechanism}/${benchmark} ${msg_size}B ${placement} ..."

    perf stat -e "$EVENTS" -x, -o "$perf_raw" -- "$@" >/dev/null 2>>"$perf_raw" || true

    local parsed
    parsed=$(parse_perf_output "$perf_raw")
    echo "${mechanism},${benchmark},${msg_size},${placement},${parsed}" >> "$OUT_CSV"
    echo "done"
}

cat > "$OUT_CSV" << 'EOF'
mechanism,benchmark,msg_size_bytes,placement,cycles,instructions,ipc,cache_misses,cache_refs,cache_miss_rate,dtlb_load_misses,dtlb_loads,dtlb_miss_rate,context_switches,migrations
EOF

echo "============================================================"
echo " perf_wrap.sh — System V IPC/TLB profiling"
echo "============================================================"
echo " Events: $EVENTS"
echo " Output: $OUT_CSV"
echo "============================================================"

CPU_PAIR="0,1"
if [[ -f "results/topo.txt" ]]; then
    cs_line=$(grep "cross_socket" results/topo.txt 2>/dev/null | head -1 || true)
    if [[ -n "$cs_line" ]]; then
        CPU_PAIR=$(echo "$cs_line" | grep -oP 'CPUs \K[0-9]+,[0-9]+' || echo "0,1")
    fi
fi

echo "  Using CPU pair: $CPU_PAIR"

echo "[1/4] Ping-Pong — System V MQ"
for sz in 64 1024 8192; do
    run_perf "mq_sysv" "pingpong" "$sz" "same_socket" \
        "$PP" -n "$PP_ITERS" -s "$sz" -c 0,1 -p same_socket -r 1
    run_perf "mq_sysv" "pingpong" "$sz" "cross_socket" \
        "$PP" -n "$PP_ITERS" -s "$sz" -c "$CPU_PAIR" -p cross_socket -r 1
done

echo ""
echo "[2/4] Ping-Pong — Shared Memory (System V)"
for sz in 64 1024 8192; do
    run_perf "shm_sysv" "pingpong" "$sz" "same_socket" \
        "$SHM" -n "$PP_ITERS" -s "$sz" -c 0,1 -p same_socket -r 1
    run_perf "shm_sysv" "pingpong" "$sz" "cross_socket" \
        "$SHM" -n "$PP_ITERS" -s "$sz" -c "$CPU_PAIR" -p cross_socket -r 1
done

echo ""
echo "[3/4] Throughput — System V MQ (depth=64)"
for sz in 64 1024 8192; do
    run_perf "mq_sysv" "throughput" "$sz" "cross_socket" \
        "$TP" -n "$TP_MSGS" -s "$sz" -q 64 -c "$CPU_PAIR" -p cross_socket -r 1
done

echo ""
echo "[4/4] Fan-in scalability — System V MQ"
for n in 1 4 8; do
    run_perf "mq_sysv" "fanin_n${n}" "64" "fanin" \
        "$SC" -m c1 -n "$n" -k "$SC_K" -s 64 -p fanin -r 1
done

echo ""
echo "============================================================"
echo " perf profiling complete."
echo " Output: $OUT_CSV"
echo " Raw perf files: $PERF_DIR/"
echo "============================================================"
