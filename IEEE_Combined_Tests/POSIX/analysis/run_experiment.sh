#!/usr/bin/env bash
# =============================================================================
# run_experiment.sh — IEEE-level benchmark orchestrator
#
# Usage:
#   bash analysis/run_experiment.sh [options]
#
#   --runs N        repetitions per configuration (default: 30)
#   --section N     run only section N (1-5); omit to run all
#   --quick         3 runs, reduced iteration counts (smoke test)
#   --help          print this message
#
# Sections:
#   1 — Ping-Pong Latency:  POSIX MQ
#   2 — Ping-Pong Latency:  Shared Memory baseline
#   3 — Throughput:         POSIX MQ (depth 1 / 64 / 512)
#   4 — Scalability:        Fan-in + Parallel pairs
#   5 — Priority Tests:     T1 head-of-line, T2 backpressure, T3 PPR sweep
#
# Running sections independently:
#   make section1          — runs section 1, writes header + rows
#   make section4          — appends section 4 rows (header already exists)
#   make experiment        — all 5 sections in sequence
#
# Output:
#   results/all_runs.csv       — all benchmark rows (single shared CSV)
#   results/memory_profile.csv — rows where mem_delta_kb != 0
#   results/topo.txt           — machine topology for paper's Table I
#   results/degradation_warnings.log — depth auto-degradation notices
# =============================================================================

set -euo pipefail

# ── Fix 1: EMFILE prevention ─────────────────────────────────────────────────
# After ~60 benchmark runs the shell inherits enough open fds to hit the
# default limit of 1024, causing mq_open to return EMFILE.
# Raising to 65536 eliminates this regardless of sweep length.
ulimit -n 65536 2>/dev/null || true
# ── Fix 2: CPU Frequency Isolation ───────────────────────────────────────────
# Lock CPUs to 'performance' governor to prevent dynamic frequency scaling 
# from skewing latency percentiles across the 30 runs.
if [ -d "/sys/devices/system/cpu/cpu0/cpufreq" ]; then
    echo "Locking CPU frequency governor to 'performance'..."
    echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null
fi	

# ── Defaults ─────────────────────────────────────────────────────────────────
RUNS=30
SECTION=""        # empty = run all sections
RESDIR="results"
CSV="${RESDIR}/all_runs.csv"
MEM_CSV="${RESDIR}/memory_profile.csv"
TOPO_TXT="${RESDIR}/topo.txt"
WARN_LOG="${RESDIR}/degradation_warnings.log"

PP="bin/pingpong"
TP="bin/throughput"
SC="bin/scalability"
SHM="bin/shm_pingpong"
PRIO="bin/priority_test"

PP_ITERS=50000
TP_MSGS=200000
SC_K=10000
PRIO_N=5000

SIZES=(8 64 256 1024 4096 8192)

# ── Argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --runs=*)    RUNS="${1#--runs=}" ;;
        --runs)      shift; RUNS="$1" ;;
        --section=*) SECTION="${1#--section=}" ;;
        --section)   shift; SECTION="$1" ;;
        --quick)     RUNS=3; PP_ITERS=5000; TP_MSGS=20000; SC_K=1000; PRIO_N=500 ;;
        --help)      sed -n '3,30p' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
    shift
done

# ── Preflight ─────────────────────────────────────────────────────────────────
for bin in "$PP" "$TP" "$SC" "$SHM" "$PRIO"; do
    [[ -x "$bin" ]] || { echo "ERROR: $bin not found. Run 'make all' first." >&2; exit 1; }
done
mkdir -p "$RESDIR"

# ── NUMA topology detection ───────────────────────────────────────────────────
probe_topology() {
    local ncpus; ncpus=$(nproc)
    declare -A cpu_node cpu_core

    for ((c=0; c<ncpus && c<512; c++)); do
        cpu_node[$c]=$(cat "/sys/devices/system/cpu/cpu${c}/topology/physical_package_id" 2>/dev/null || echo 0)
        cpu_core[$c]=$(cat "/sys/devices/system/cpu/cpu${c}/topology/core_id"             2>/dev/null || echo $c)
    done

    TOPO_SAME_CORE="-1,-1"
    TOPO_SAME_SOCKET="-1,-1"
    TOPO_CROSS_SOCKET="-1,-1"

    for ((a=0; a<ncpus; a++)); do
        for ((b=a+1; b<ncpus; b++)); do
            if [[ "${cpu_node[$a]}" == "${cpu_node[$b]}" && "${cpu_core[$a]}" == "${cpu_core[$b]}" ]]; then
                TOPO_SAME_CORE="${a},${b}"; break 2
            fi
        done
    done
    for ((a=0; a<ncpus; a++)); do
        for ((b=a+1; b<ncpus; b++)); do
            if [[ "${cpu_node[$a]}" == "${cpu_node[$b]}" && "${cpu_core[$a]}" != "${cpu_core[$b]}" ]]; then
                TOPO_SAME_SOCKET="${a},${b}"; break 2
            fi
        done
    done
    for ((a=0; a<ncpus; a++)); do
        for ((b=a+1; b<ncpus; b++)); do
            if [[ "${cpu_node[$a]}" != "${cpu_node[$b]}" ]]; then
                TOPO_CROSS_SOCKET="${a},${b}"; break 2
            fi
        done
    done

    {
        echo "Machine topology summary"
        echo "Generated: $(date)"
        echo "Logical CPUs: $ncpus"
        echo "NUMA nodes: $(printf '%s\n' "${cpu_node[@]}" | sort -u | wc -l)"
        echo ""
        echo "Placement pairs selected:"
        echo "  same_core    : CPUs ${TOPO_SAME_CORE}"
        echo "  same_socket  : CPUs ${TOPO_SAME_SOCKET}"
        echo "  cross_socket : CPUs ${TOPO_CROSS_SOCKET}"
        echo ""
        echo "CPU detail:"
        for ((c=0; c<ncpus; c++)); do
            printf "  cpu%d → node%d core%d\n" "$c" "${cpu_node[$c]:-0}" "${cpu_core[$c]:-$c}"
        done
    } | tee "$TOPO_TXT" >&2

    export TOPO_SAME_CORE TOPO_SAME_SOCKET TOPO_CROSS_SOCKET
}

# ── Run helpers ───────────────────────────────────────────────────────────────
# Standard: stderr goes to terminal (fine for most benchmarks)
run_n_times() {
    local bin="$1"; shift
    local desc="$1"; shift
    printf "  %-50s " "$desc ..."
    for ((r=1; r<=RUNS; r++)); do
        "$bin" -r "$r" "$@" >> "$CSV"
    done
    echo "done (${RUNS} runs)"
}

# Quiet: stderr deduped to log file (used for throughput to suppress repeated
# depth-degradation warnings that would flood 30x per configuration)
run_n_times_quiet() {
    local bin="$1"; shift
    local desc="$1"; shift
    printf "  %-50s " "$desc ..."
    for ((r=1; r<=RUNS; r++)); do
        "$bin" -r "$r" "$@" >> "$CSV" 2>>"${WARN_LOG}.tmp"
    done
    if [[ -s "${WARN_LOG}.tmp" ]]; then
        sort -u "${WARN_LOG}.tmp" >> "$WARN_LOG"
        rm -f "${WARN_LOG}.tmp"
    fi
    echo "done (${RUNS} runs)"
}

# ── Print plan header ─────────────────────────────────────────────────────────
echo "============================================================"
echo " IEEE IPC Benchmark Suite"
echo "============================================================"
echo " Runs per config : $RUNS"
echo " PP iterations   : $PP_ITERS"
echo " TP messages     : $TP_MSGS"
echo " SC msgs/pair    : $SC_K"
echo " Priority msgs   : $PRIO_N"
echo " Section         : ${SECTION:-all}"
echo " Output CSV      : $CSV"
echo "============================================================"

probe_topology

# ── Raise RLIMIT_MSGQUEUE ─────────────────────────────────────────────────────
RLIMIT_MQ_NEEDED=$(( 512 * (8192 + 64) ))
if ulimit -q "${RLIMIT_MQ_NEEDED}" 2>/dev/null; then
    echo "  [rlimit] RLIMIT_MSGQUEUE raised to ${RLIMIT_MQ_NEEDED} bytes"
else
    echo "  [rlimit] RLIMIT_MSGQUEUE raise failed — depth=512 will auto-degrade." >&2
    echo "  [rlimit] To fix: echo '* hard msgqueue ${RLIMIT_MQ_NEEDED}' >> /etc/security/limits.conf" >&2
fi

# ── Build placement arrays ────────────────────────────────────────────────────
declare -a PLACEMENTS=()
declare -a PLACE_LABELS=()
[[ "$TOPO_SAME_CORE"    != "-1,-1" ]] && PLACEMENTS+=("$TOPO_SAME_CORE")    && PLACE_LABELS+=("same_core")
[[ "$TOPO_SAME_SOCKET"  != "-1,-1" ]] && PLACEMENTS+=("$TOPO_SAME_SOCKET")  && PLACE_LABELS+=("same_socket")
[[ "$TOPO_CROSS_SOCKET" != "-1,-1" ]] && PLACEMENTS+=("$TOPO_CROSS_SOCKET") && PLACE_LABELS+=("cross_socket")
[[ ${#PLACEMENTS[@]} -eq 0 ]]         && PLACEMENTS=("0,1")                  && PLACE_LABELS+=("diff_core")

# Throughput uses the highest-latency placement to stress the copy path
TP_CPUS="${PLACEMENTS[-1]}"
TP_LABEL="${PLACE_LABELS[-1]}"

# ── CSV header guard ──────────────────────────────────────────────────────────
# Write header only if the file does not already exist.
# This makes append-mode safe: running section4 after section3 never
# duplicates the header row, which would break analyze.py's CSV parser.
[[ ! -f "$CSV" ]] && "$PP" -H > "$CSV"

# ═════════════════════════════════════════════════════════════════════════════
# Section functions
# ═════════════════════════════════════════════════════════════════════════════

run_section_1() {
    echo ""
    echo "[1/5] Ping-Pong Latency — POSIX MQ"
    echo "-----------------------------------------------------------"
    for pi in "${!PLACEMENTS[@]}"; do
        cpus="${PLACEMENTS[$pi]}"; plabel="${PLACE_LABELS[$pi]}"
        echo "  Placement: $plabel (CPUs $cpus)"
        for s in "${SIZES[@]}"; do
            run_n_times "$PP" "pp mq_posix $plabel size=${s}B" \
                -n "$PP_ITERS" -s "$s" -c "$cpus" -p "$plabel"
        done
    done
}

run_section_2() {
    echo ""
    echo "[2/5] Ping-Pong Latency — Shared Memory (shm_posix)"
    echo "-----------------------------------------------------------"
    for pi in "${!PLACEMENTS[@]}"; do
        cpus="${PLACEMENTS[$pi]}"; plabel="${PLACE_LABELS[$pi]}"
        echo "  Placement: $plabel (CPUs $cpus)"
        for s in "${SIZES[@]}"; do
            run_n_times "$SHM" "pp shm_posix $plabel size=${s}B" \
                -n "$PP_ITERS" -s "$s" -c "$cpus" -p "$plabel"
        done
    done
}

run_section_3() {
    echo ""
    echo "[3/5] Throughput — POSIX MQ"
    echo "-----------------------------------------------------------"
    for depth in 1 64 512; do
        echo "  Queue depth=$depth"
        for s in "${SIZES[@]}"; do
            run_n_times_quiet "$TP" "tp depth=$depth size=${s}B" \
                -n "$TP_MSGS" -s "$s" -q "$depth" \
                -c "$TP_CPUS" -p "${TP_LABEL}_q${depth}"
        done
    done
}

run_section_4() {
    echo ""
    echo "[4/5] Scalability — POSIX MQ"
    echo "-----------------------------------------------------------"
    echo "  C1: Fan-in (N producers → 1 consumer)"
    for n in 1 2 4 8 16; do
        for s in 64 256 1024; do
            run_n_times "$SC" "fanin n=$n size=${s}B" \
                -m c1 -n "$n" -k "$SC_K" -s "$s" -p "fanin_n${n}"
        done
    done
    echo "  C2: Parallel pairs"
    for n in 1 2 4 8; do
        for s in 64 256 1024; do
            run_n_times "$SC" "pairs n=$n size=${s}B" \
                -m c2 -n "$n" -k "$SC_K" -s "$s" -p "pairs_n${n}"
        done
    done
}

run_section_5() {
    echo ""
    echo "[5/5] Priority Tests — POSIX MQ (novel contribution)"
    echo "-----------------------------------------------------------"
    echo "  T1: Priority ordering verification"
    for s in 64 256 1024; do
        run_n_times "$PRIO" "priority T1 size=${s}B" \
            -t 1 -n "$PRIO_N" -s "$s" -p "t1_${s}B"
    done
    echo "  T2: Per-priority latency distribution"
    for pi in "${!PLACEMENTS[@]}"; do
        cpus="${PLACEMENTS[$pi]}"; plabel="${PLACE_LABELS[$pi]}"
        for s in 64 256 1024; do
            run_n_times "$PRIO" "priority T2 $plabel size=${s}B" \
                -t 2 -n "$PRIO_N" -s "$s" -c "$cpus" -p "${plabel}_${s}B"
        done
    done
    echo "  T3: Multi-producer priority inversion stress"
    for n in 2 4 8; do
        for s in 64 256; do
            run_n_times "$PRIO" "priority T3 n=$n size=${s}B" \
                -t 3 -N "$n" -n "$PRIO_N" -s "$s" -p "t3_n${n}_${s}B"
        done
    done
}

# ═════════════════════════════════════════════════════════════════════════════
# Dispatch
# ═════════════════════════════════════════════════════════════════════════════
case "$SECTION" in
    1) run_section_1 ;;
    2) run_section_2 ;;
    3) run_section_3 ;;
    4) run_section_4 ;;
    5) run_section_5 ;;
    "")
        run_section_1
        run_section_2
        run_section_3
        run_section_4
        run_section_5
        ;;
    *)
        echo "ERROR: unknown --section '$SECTION'. Use 1-5." >&2
        exit 1
        ;;
esac

# ── Memory profile extraction ─────────────────────────────────────────────────
python3 -c "
import csv
rows = []
with open('${CSV}') as f:
    for row in csv.DictReader(f):
        if int(row.get('mem_delta_kb', 0)) != 0:
            rows.append(row)
if rows:
    with open('${MEM_CSV}', 'w') as out:
        w = csv.DictWriter(out, fieldnames=rows[0].keys())
        w.writeheader(); w.writerows(rows)
    print(f'  Memory profile: {len(rows)} rows written to ${MEM_CSV}')
else:
    print('  No memory delta rows found.')
" 2>/dev/null || true

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "============================================================"
total_rows=$(wc -l < "$CSV")
echo " Done. CSV rows: $total_rows (1 header + $((total_rows - 1)) data)"
echo " Output CSV  : $CSV"
echo " Topology    : $TOPO_TXT"
[[ -f "$MEM_CSV" ]] && echo " Memory prof : $MEM_CSV"
[[ -s "$WARN_LOG" ]] && echo " Warnings    : $WARN_LOG  (depth auto-degradation)"
echo " Next step   : python3 analysis/analyze.py --input $CSV"
echo "============================================================"
