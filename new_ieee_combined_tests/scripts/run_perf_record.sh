#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────
# run_perf_record.sh — capture call-graph profiles for representative
#                      configurations to identify hot kernel functions.
#
# Output: results/perf_record/<label>.txt
# Each file is the text-mode `perf report --stdio` output, with kernel
# and user-space symbols resolved.  Both flat (top-N functions) and
# call-graph (--children) views are emitted into the same file.
#
# Why these six configurations:
#   1. SysV  fan-in N=16 64B          — locate native_queued_spin_lock_slowpath
#   2. POSIX fan-in N=16 64B          — symmetric POSIX call graph
#   3. POSIX ping-pong diff_l3 64B    — investigate cross-CCX p99.9 anomaly
#   4. SysV  ping-pong diff_l3 64B    — symmetric for cross-CCX
#   5. POSIX throughput same_core q=8 — steady-state batched throughput
#   6. SysV  throughput same_core q=8 — symmetric batched throughput
# ─────────────────────────────────────────────────────────────────────────
set -eu
# Note: pipefail intentionally NOT set.  `head -N` truncating the output of
# `perf report` causes SIGPIPE on the upstream command, which under pipefail
# would be treated as a pipeline failure and (with set -e) silently exit
# the entire script after the first config.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="${ROOT_DIR}/results/perf_record"
mkdir -p "$OUTDIR"

POSIX_BIN="${ROOT_DIR}/stacks/POSIX/bin"
SYSV_BIN="${ROOT_DIR}/stacks/SYSV/bin"

# ── Sanity: binaries built? ──────────────────────────────────────────
for b in pingpong throughput scalability; do
    [[ -x "${POSIX_BIN}/${b}" ]] || {
        echo "Missing ${POSIX_BIN}/${b} — run 'make run' first" >&2; exit 1; }
    [[ -x "${SYSV_BIN}/${b}"  ]] || {
        echo "Missing ${SYSV_BIN}/${b} — run 'make run' first"  >&2; exit 1; }
done

# ── Sanity: perf usable? ─────────────────────────────────────────────
if ! command -v perf &>/dev/null; then
    echo "perf not installed.  sudo apt install linux-tools-\$(uname -r)" >&2
    exit 1
fi
paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 4)
if [[ "$paranoid" -gt 0 ]]; then
    echo "perf_event_paranoid=${paranoid} — running setup_perf.sh first..." >&2
    bash "${ROOT_DIR}/scripts/setup_perf.sh" || true
fi

# ── Topology pairs ───────────────────────────────────────────────────
eval "$(bash "${ROOT_DIR}/scripts/detect_cpu_pairs.sh" --export)"
SC="${SAME_CORE_CPUS:-0,1}"
DL="${DIFF_L3_CPUS:-${CROSS_CORE_CPUS:-0,1}}"

echo "Topology:"
echo "  same_core CPUs : ${SC}"
echo "  diff_l3   CPUs : ${DL}"
echo ""

# ── Helpers ──────────────────────────────────────────────────────────
CONFIG_NUM=0
run_one() {
    local label="$1"; shift
    local bin="$1"; shift
    local args=("$@")

    CONFIG_NUM=$((CONFIG_NUM + 1))
    local datafile="${OUTDIR}/${label}.data"
    local report="${OUTDIR}/${label}.txt"
    local recordlog="${OUTDIR}/${label}.recordlog"

    echo ""
    echo "─────────────────────────────────────────────────────────────"
    echo "[${CONFIG_NUM}/6] ${label}"
    echo "─────────────────────────────────────────────────────────────"
    echo "    bin  : ${bin}"
    echo "    args : ${args[*]}"

    # Skip if a non-trivial report already exists (resume after partial run)
    if [[ -s "$report" ]] && [[ $(wc -l < "$report") -gt 100 ]]; then
        echo "    SKIP — ${report} already exists with $(wc -l < "$report") lines"
        return 0
    fi

    # Frame-pointer call graph at ~1 kHz sample rate.
    # Even with -O2 omitting frame pointers in user-space, kernel-side
    # symbols resolve fine — and that's what we need to find
    # native_queued_spin_lock_slowpath, futex_wake, etc.
    echo "    [perf record] starting..."
    if ! perf record -F 999 -g --call-graph fp \
            -o "$datafile" \
            -- "$bin" "${args[@]}" \
            > "$recordlog" 2>&1
    then
        echo "    FAILED perf record — last 10 lines of ${recordlog}:" >&2
        tail -10 "$recordlog" | sed 's/^/        /' >&2
        echo "    Trying binary directly to capture exit reason..."
        "$bin" "${args[@]}" >"${OUTDIR}/${label}.binstdout" 2>"${OUTDIR}/${label}.binstderr" || true
        echo "    -> last 10 lines of ${OUTDIR}/${label}.binstderr:" >&2
        tail -10 "${OUTDIR}/${label}.binstderr" 2>/dev/null | sed 's/^/        /' >&2 || true
        return 0
    fi
    echo "    [perf record] done ($(du -h "$datafile" | cut -f1))"

    # awk 'NR<=N' instead of `head -N` because awk reads to EOF and does not
    # cause SIGPIPE on the upstream command.
    {
        echo "########################################################"
        echo "# ${label}"
        echo "# bin  : ${bin}"
        echo "# args : ${args[*]}"
        echo "# date : $(date -Iseconds)"
        echo "########################################################"
        echo ""
        echo "## Top 50 functions (FLAT, no children)"
        echo "## Each line: percent  samples  obj  symbol"
        echo ""
        perf report --stdio --no-children -i "$datafile" 2>/dev/null \
            | awk 'NR<=200'
        echo ""
        echo ""
        echo "## Top 30 call chains (WITH CHILDREN, hot-path roll-up)"
        echo ""
        perf report --stdio --children -i "$datafile" 2>/dev/null \
            | awk 'NR<=150'
    } > "$report"

    local lines
    lines=$(wc -l < "$report")
    echo "    -> ${report} (${lines} lines)"

    # Keep .data files in case flame graphs are wanted later.
    # Uncomment the next line to delete them and save disk:
    # rm -f "$datafile"
}

# ── 1. Fan-in collapse: SysV N=16 (the headline scalability finding) ─
run_one "01_sysv_fanin_n16_64B" \
    "${SYSV_BIN}/scalability" \
    -r 1 -m c1 -n 16 -k 10000 -s 64 -p fanin_n16

# ── 2. Fan-in: POSIX N=16 (symmetric comparison) ─────────────────────
run_one "02_posix_fanin_n16_64B" \
    "${POSIX_BIN}/scalability" \
    -r 1 -m c1 -n 16 -k 10000 -s 64 -p fanin_n16

# ── 3. Cross-CCX ping-pong: POSIX (the p99.9 anomaly cell) ───────────
run_one "03_posix_pingpong_diff_l3_64B" \
    "${POSIX_BIN}/pingpong" \
    -r 1 -n 50000 -s 64 -c "$DL" -p diff_l3

# ── 4. Cross-CCX ping-pong: SysV (symmetric for the anomaly) ─────────
run_one "04_sysv_pingpong_diff_l3_64B" \
    "${SYSV_BIN}/pingpong" \
    -r 1 -n 50000 -s 64 -c "$DL" -p diff_l3

# ── 5. Same-core throughput at q=8: POSIX ────────────────────────────
run_one "05_posix_throughput_samecore_64B_q8" \
    "${POSIX_BIN}/throughput" \
    -r 1 -n 200000 -s 64 -q 8 -c "$SC" -p same_core_q8

# ── 6. Same-core throughput at q=8: SysV ─────────────────────────────
run_one "06_sysv_throughput_samecore_64B_q8" \
    "${SYSV_BIN}/throughput" \
    -r 1 -n 200000 -s 64 -q 8 -c "$SC" -p same_core_q8

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "Reports in ${OUTDIR}/"
ls -la "${OUTDIR}/"*.txt 2>/dev/null || true
echo ""
echo "Disk used by .data files (kept for optional flame graphs):"
du -sh "${OUTDIR}/"*.data 2>/dev/null | tail -10 || echo "  (no .data files)"
echo ""
echo "If disk space is tight: rm ${OUTDIR}/*.data"
