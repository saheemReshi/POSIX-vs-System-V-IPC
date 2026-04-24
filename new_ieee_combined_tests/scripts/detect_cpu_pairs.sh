#!/usr/bin/env bash
set -euo pipefail

# Detect two CPU pairs for laptop-style benchmarking:
# 1) same_core  -> two logical CPUs sharing one physical core (SMT siblings)
# 2) cross_core -> two CPUs on different cores (same socket if possible)

MODE="${1:---export}"

declare -a CPUS=()
declare -A CORE_OF
declare -A PKG_OF

for cpu_path in /sys/devices/system/cpu/cpu[0-9]*; do
    cpu="${cpu_path##*cpu}"
    [[ "$cpu" =~ ^[0-9]+$ ]] || continue
    CPUS+=("$cpu")
    CORE_OF[$cpu]="$(cat "$cpu_path/topology/core_id" 2>/dev/null || echo "$cpu")"
    PKG_OF[$cpu]="$(cat "$cpu_path/topology/physical_package_id" 2>/dev/null || echo 0)"
done

if [[ ${#CPUS[@]} -lt 2 ]]; then
    echo "Need at least 2 CPUs to run ping-pong benchmarks." >&2
    exit 1
fi

# Numeric sort to keep deterministic CPU pair selection.
mapfile -t CPUS < <(printf '%s\n' "${CPUS[@]}" | sort -n)

same_core=""
cross_core=""

for ((i=0; i<${#CPUS[@]}; i++)); do
    for ((j=i+1; j<${#CPUS[@]}; j++)); do
        a="${CPUS[$i]}"
        b="${CPUS[$j]}"

        if [[ -z "$same_core" && "${PKG_OF[$a]}" == "${PKG_OF[$b]}" && "${CORE_OF[$a]}" == "${CORE_OF[$b]}" ]]; then
            same_core="$a,$b"
        fi

        if [[ -z "$cross_core" && "${CORE_OF[$a]}" != "${CORE_OF[$b]}" ]]; then
            # Prefer same-socket cross-core to avoid NUMA.
            if [[ "${PKG_OF[$a]}" == "${PKG_OF[$b]}" ]]; then
                cross_core="$a,$b"
            fi
        fi

        [[ -n "$same_core" && -n "$cross_core" ]] && break 2
    done
done

# Fallbacks for machines without SMT siblings or unusual topology.
if [[ -z "$same_core" ]]; then
    same_core="${CPUS[0]},${CPUS[1]}"
fi
if [[ -z "$cross_core" ]]; then
    cross_core="${CPUS[0]},${CPUS[1]}"
fi

if [[ "$MODE" == "--summary" ]]; then
    # shellcheck disable=SC2154
    sockets="$(printf '%s\n' "${PKG_OF[@]}" | sort -u | wc -l)"
    echo "Logical CPUs : ${#CPUS[@]}"
    echo "Sockets      : ${sockets}"
    echo "same_core    : ${same_core}"
    echo "cross_core   : ${cross_core}"
    exit 0
fi

if [[ "$MODE" == "--export" ]]; then
    cat <<EOF
SAME_CORE_CPUS='${same_core}'
CROSS_CORE_CPUS='${cross_core}'
EOF
    exit 0
fi

echo "Unknown mode: ${MODE}" >&2
echo "Use --export (default) or --summary." >&2
exit 1
