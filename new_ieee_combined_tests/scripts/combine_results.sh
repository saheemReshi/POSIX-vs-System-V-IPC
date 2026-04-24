#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

POSIX_CSV="${ROOT_DIR}/results/raw/posix/all_runs.csv"
SYSV_CSV="${ROOT_DIR}/results/raw/sysv/all_runs.csv"
OUT_CSV="${ROOT_DIR}/results/raw/combined/all_runs.csv"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --posix)  shift; POSIX_CSV="$1" ;;
        --sysv)   shift; SYSV_CSV="$1" ;;
        --out)    shift; OUT_CSV="$1" ;;
        --help)
            cat <<EOF
Usage: bash scripts/combine_results.sh [options]

Options:
  --posix FILE   POSIX CSV path (default: ${POSIX_CSV})
  --sysv  FILE   SysV CSV path (default: ${SYSV_CSV})
  --out   FILE   Output combined CSV path (default: ${OUT_CSV})
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

mkdir -p "$(dirname "$OUT_CSV")"

wrote_header=0
: > "$OUT_CSV"

append_csv() {
    local in_csv="$1"
    if [[ ! -f "$in_csv" ]]; then
        return 0
    fi

    if [[ $wrote_header -eq 0 ]]; then
        head -n 1 "$in_csv" > "$OUT_CSV"
        wrote_header=1
    fi

    tail -n +2 "$in_csv" >> "$OUT_CSV"
}

append_csv "$POSIX_CSV"
append_csv "$SYSV_CSV"

if [[ $wrote_header -eq 0 ]]; then
    echo "No input CSVs found. Nothing to combine." >&2
    exit 1
fi

rows=$(( $(wc -l < "$OUT_CSV") - 1 ))
echo "Combined CSV written: ${OUT_CSV}"
echo "Data rows: ${rows}"
