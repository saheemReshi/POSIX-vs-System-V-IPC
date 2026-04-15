#ifndef AFFINITY_H
#define AFFINITY_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Basic CPU pinning ──────────────────────────────────────────────────── */

static inline void pin_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        perror("sched_setaffinity (non-fatal)");
}

static inline int num_cpus(void)
{
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
}

/* ── NUMA topology probing ──────────────────────────────────────────────── */
/*
 * Reads NUMA node assignments from:
 *   /sys/devices/system/cpu/cpuN/topology/physical_package_id
 *
 * This gives us the physical socket (NUMA node) each logical CPU belongs to.
 * We use this to classify CPU pairs for NUMA-aware placement:
 *
 *   same_core   — both threads on the same physical core (hyper-threading)
 *   same_socket — different cores, same NUMA node
 *   cross_socket— different NUMA nodes (cross-socket, highest latency)
 *
 * Falls back gracefully on single-socket machines: all CPUs report node 0,
 * so cross_socket is never available and same_socket is always used.
 */

#define MAX_CPUS 512

typedef struct {
    int cpu_to_node[MAX_CPUS];   /* NUMA node for each logical CPU */
    int cpu_to_core[MAX_CPUS];   /* physical core id for each logical CPU */
    int n_cpus;
    int n_nodes;
} topo_t;

/* Read an integer from a sysfs topology file for a given CPU. */
static inline int _sysfs_topo_int(int cpu, const char *field)
{
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, field);
    FILE *f = fopen(path, "r");
    if (!f) return 0;  /* fallback: assume node/core 0 */
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = 0;
    fclose(f);
    return val;
}

static inline topo_t topo_probe(void)
{
    topo_t t;
    memset(&t, 0, sizeof(t));
    t.n_cpus  = num_cpus();
    t.n_nodes = 0;

    if (t.n_cpus > MAX_CPUS) t.n_cpus = MAX_CPUS;

    for (int c = 0; c < t.n_cpus; c++) {
        t.cpu_to_node[c] = _sysfs_topo_int(c, "physical_package_id");
        t.cpu_to_core[c] = _sysfs_topo_int(c, "core_id");
        if (t.cpu_to_node[c] + 1 > t.n_nodes)
            t.n_nodes = t.cpu_to_node[c] + 1;
    }
    return t;
}

/*
 * Placement label for a CPU pair.
 *
 * Returns one of:
 *   "same_core"    — hyperthreads on the same physical core
 *   "same_socket"  — different cores, same NUMA node
 *   "cross_socket" — different NUMA nodes
 */
static inline const char *topo_placement(const topo_t *t, int cpu0, int cpu1)
{
    if (cpu0 < 0 || cpu1 < 0 ||
        cpu0 >= t->n_cpus || cpu1 >= t->n_cpus)
        return "unspecified";

    if (t->cpu_to_node[cpu0] != t->cpu_to_node[cpu1])
        return "cross_socket";

    if (t->cpu_to_core[cpu0] == t->cpu_to_core[cpu1])
        return "same_core";

    return "same_socket";
}

/*
 * Find the first CPU that belongs to a different NUMA node than `cpu`.
 * Returns -1 if the machine has only one NUMA node (single socket).
 */
static inline int topo_first_cpu_on_other_node(const topo_t *t, int cpu)
{
    int my_node = t->cpu_to_node[cpu];
    for (int c = 0; c < t->n_cpus; c++) {
        if (c != cpu && t->cpu_to_node[c] != my_node)
            return c;
    }
    return -1;
}

/*
 * Find the first CPU on the same NUMA node as `cpu` but on a different
 * physical core. Returns -1 if no such CPU exists.
 */
static inline int topo_first_cpu_same_socket_diff_core(const topo_t *t, int cpu)
{
    int my_node = t->cpu_to_node[cpu];
    int my_core = t->cpu_to_core[cpu];
    for (int c = 0; c < t->n_cpus; c++) {
        if (c != cpu &&
            t->cpu_to_node[c] == my_node &&
            t->cpu_to_core[c] != my_core)
            return c;
    }
    return -1;
}

/*
 * Print the NUMA topology summary to stderr for the paper's system description
 * section. Called once at the start of run_experiment.sh.
 */
static inline void topo_print(const topo_t *t)
{
    fprintf(stderr, "[topo] %d logical CPUs, %d NUMA node(s)\n",
            t->n_cpus, t->n_nodes);
    for (int c = 0; c < t->n_cpus; c++) {
        fprintf(stderr, "  cpu%d → node%d core%d\n",
                c, t->cpu_to_node[c], t->cpu_to_core[c]);
    }
}

#endif /* AFFINITY_H */
