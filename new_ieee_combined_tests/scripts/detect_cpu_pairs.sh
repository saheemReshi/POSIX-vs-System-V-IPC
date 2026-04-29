#!/usr/bin/env bash
set -euo pipefail

# Detect topology-aware CPU pairs.  Exports up to four placement axes,
# any of which may be empty on machines that do not have that level
# of cache-sharing differentiation.
#
#   SAME_CORE_CPUS    SMT siblings (same physical core)
#   SAME_L2_CPUS      different cores, same L2 cluster (e.g. Zen "ccx" L2 group,
#                     Apple/Arm cluster, Alder Lake P/E sub-cluster)
#   SAME_L3_CPUS      different L2 clusters but same L3 (most consumer CPUs)
#   DIFF_L3_CPUS      different L3 caches (cross-CCX on Ryzen, multi-socket etc.)
#
# Backward compat: CROSS_CORE_CPUS is also exported and equals the first
# of {SAME_L3_CPUS, SAME_L2_CPUS, DIFF_L3_CPUS} that exists.

MODE="${1:---export}"

declare -a CPUS=()
declare -A CORE_OF
declare -A PKG_OF
declare -A L2_OF      # leader CPU id of the L2 cluster this CPU belongs to
declare -A L3_OF      # leader CPU id of the L3 cluster this CPU belongs to

# Find the cache index that corresponds to a given level (L2 or L3).
cache_index_for_level() {
    local cpu="$1" want_level="$2"
    local idx
    for idx in /sys/devices/system/cpu/cpu${cpu}/cache/index*; do
        [[ -d "$idx" ]] || continue
        local level
        level=$(cat "$idx/level" 2>/dev/null || echo "")
        if [[ "$level" == "$want_level" ]]; then
            echo "$idx"
            return 0
        fi
    done
    return 1
}

# Convert a shared_cpu_list like "0-3,8-11" to a leader (smallest CPU id).
leader_of_list() {
    local list="$1"
    # Take first numeric token, strip any range
    local first
    first="${list%%,*}"
    first="${first%%-*}"
    echo "$first"
}

for cpu_path in /sys/devices/system/cpu/cpu[0-9]*; do
    cpu="${cpu_path##*cpu}"
    [[ "$cpu" =~ ^[0-9]+$ ]] || continue
    [[ -d "$cpu_path/topology" ]] || continue

    CPUS+=("$cpu")
    CORE_OF[$cpu]="$(cat "$cpu_path/topology/core_id" 2>/dev/null || echo "$cpu")"
    PKG_OF[$cpu]="$(cat "$cpu_path/topology/physical_package_id" 2>/dev/null || echo 0)"

    if l2_idx=$(cache_index_for_level "$cpu" 2); then
        list=$(cat "$l2_idx/shared_cpu_list" 2>/dev/null || echo "$cpu")
        L2_OF[$cpu]="$(leader_of_list "$list")"
    else
        L2_OF[$cpu]="$cpu"
    fi

    if l3_idx=$(cache_index_for_level "$cpu" 3); then
        list=$(cat "$l3_idx/shared_cpu_list" 2>/dev/null || echo "$cpu")
        L3_OF[$cpu]="$(leader_of_list "$list")"
    else
        # No L3 visible (e.g. some embedded chips) — fall back to package id
        L3_OF[$cpu]="${PKG_OF[$cpu]}"
    fi
done

if [[ ${#CPUS[@]} -lt 2 ]]; then
    echo "Need at least 2 CPUs." >&2
    exit 1
fi

mapfile -t CPUS < <(printf '%s\n' "${CPUS[@]}" | sort -n)

same_core=""
same_l2=""
same_l3=""
diff_l3=""

for ((i=0; i<${#CPUS[@]}; i++)); do
    a="${CPUS[$i]}"
    for ((j=i+1; j<${#CPUS[@]}; j++)); do
        b="${CPUS[$j]}"

        same_socket=0
        [[ "${PKG_OF[$a]}" == "${PKG_OF[$b]}" ]] && same_socket=1

        # 1) SMT siblings — same physical core
        if [[ -z "$same_core" && "$same_socket" -eq 1 \
              && "${CORE_OF[$a]}" == "${CORE_OF[$b]}" ]]; then
            same_core="$a,$b"
            continue
        fi

        # Different cores from here on
        if [[ "${CORE_OF[$a]}" == "${CORE_OF[$b]}" ]]; then
            continue
        fi

        # 2) Same L2 cluster, different cores
        if [[ -z "$same_l2" && "$same_socket" -eq 1 \
              && "${L2_OF[$a]}" == "${L2_OF[$b]}" ]]; then
            same_l2="$a,$b"
            continue
        fi

        # 3) Different L2, same L3 (same socket)
        if [[ -z "$same_l3" && "$same_socket" -eq 1 \
              && "${L2_OF[$a]}" != "${L2_OF[$b]}" \
              && "${L3_OF[$a]}" == "${L3_OF[$b]}" ]]; then
            same_l3="$a,$b"
            continue
        fi

        # 4) Different L3 (cross-CCX or cross-socket)
        if [[ -z "$diff_l3" && "${L3_OF[$a]}" != "${L3_OF[$b]}" ]]; then
            diff_l3="$a,$b"
            continue
        fi
    done
done

# Backward-compat aggregate "cross_core" — first non-SMT pair we found
cross_core=""
for cand in "$same_l2" "$same_l3" "$diff_l3"; do
    if [[ -n "$cand" ]]; then cross_core="$cand"; break; fi
done

# Same-core fallback if SMT off or single-thread CPU
if [[ -z "$same_core" ]]; then
    same_core="${CPUS[0]},${CPUS[1]}"
fi
# Cross-core fallback
if [[ -z "$cross_core" ]]; then
    cross_core="${CPUS[0]},${CPUS[1]}"
fi

if [[ "$MODE" == "--summary" ]]; then
    sockets="$(printf '%s\n' "${PKG_OF[@]}" | sort -u | wc -l)"
    l3_groups="$(printf '%s\n' "${L3_OF[@]}" | sort -u | wc -l)"
    l2_groups="$(printf '%s\n' "${L2_OF[@]}" | sort -u | wc -l)"
    echo "Logical CPUs : ${#CPUS[@]}"
    echo "Sockets      : ${sockets}"
    echo "L3 clusters  : ${l3_groups}"
    echo "L2 clusters  : ${l2_groups}"
    echo "same_core    : ${same_core}"
    echo "same_l2      : ${same_l2:-<none>}"
    echo "same_l3      : ${same_l3:-<none>}"
    echo "diff_l3      : ${diff_l3:-<none>}"
    echo "cross_core*  : ${cross_core}    (* = backward compat alias)"
    exit 0
fi

if [[ "$MODE" == "--export" ]]; then
    cat <<EOF
SAME_CORE_CPUS='${same_core}'
SAME_L2_CPUS='${same_l2}'
SAME_L3_CPUS='${same_l3}'
DIFF_L3_CPUS='${diff_l3}'
CROSS_CORE_CPUS='${cross_core}'
EOF
    exit 0
fi

echo "Unknown mode: ${MODE}" >&2
echo "Use --export (default) or --summary." >&2
exit 1
