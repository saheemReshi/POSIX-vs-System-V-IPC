#ifndef STATS_H
#define STATS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ── Sample allocation ──────────────────────────────────────────────────── */

static inline uint64_t *alloc_samples(uint64_t n)
{
    uint64_t *s = malloc(n * sizeof(uint64_t));
    if (!s) { perror("malloc samples"); exit(1); }
    return s;
}

/* ── Memory footprint profiling via /proc/self/status ───────────────────── */
/*
 * We read VmRSS (Resident Set Size) and VmPeak from /proc/self/status.
 * VmRSS is the most meaningful metric: it measures physical RAM actually
 * in use at measurement time. VmPeak is the historical high-water mark.
 *
 * Usage pattern in a benchmark:
 *
 *   long mem_before = read_vmrss_kb();
 *   ... benchmark work ...
 *   long mem_after = read_vmrss_kb();
 *   r.mem_delta_kb = mem_after - mem_before;
 *
 * The delta captures the kernel data structures (message queue descriptors,
 * internal queue buffers) added while the benchmark ran.
 *
 * NOTE: In a forked child the /proc entry is for the child process.
 *       For inter-process IPC the parent reads mem before and after
 *       waitpid(), which captures the steady-state parent overhead.
 *       Kernel-internal queue pages are charged to the process that
 *       created the queue via mq_open(O_CREAT).
 */

typedef struct {
    long vmrss_kb;   /* resident set size at snapshot time */
    long vmpeak_kb;  /* peak virtual memory size           */
} mem_snapshot_t;

/* Read a single field from /proc/self/status by exact prefix. */
static inline long _read_proc_field(const char *field)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;

    char  line[256];
    long  value = -1;
    size_t flen = strlen(field);

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, field, flen) == 0) {
            /* Format: "VmRSS:\t  1234 kB\n" */
            sscanf(line + flen, "%ld", &value);
            break;
        }
    }
    fclose(f);
    return value;
}

static inline mem_snapshot_t mem_snapshot(void)
{
    mem_snapshot_t snap;
    snap.vmrss_kb  = _read_proc_field("VmRSS:");
    snap.vmpeak_kb = _read_proc_field("VmPeak:");
    return snap;
}

static inline long mem_delta_kb(const mem_snapshot_t *before,
                                 const mem_snapshot_t *after)
{
    if (before->vmrss_kb < 0 || after->vmrss_kb < 0) return 0;
    return after->vmrss_kb - before->vmrss_kb;
}

/* Print a standalone memory profiling CSV row (used by perf wrapper) */
static inline void print_mem_csv_header(void)
{
    printf("mechanism,benchmark,msg_size_bytes,placement,"
           "vmrss_before_kb,vmrss_after_kb,delta_kb,vmpeak_kb\n");
}

static inline void print_mem_csv_row(const char *mechanism,
                                      const char *bench,
                                      size_t      msg_size,
                                      const char *placement,
                                      const mem_snapshot_t *before,
                                      const mem_snapshot_t *after)
{
    printf("%s,%s,%zu,%s,%ld,%ld,%ld,%ld\n",
           mechanism, bench, msg_size, placement,
           before->vmrss_kb, after->vmrss_kb,
           mem_delta_kb(before, after),
           after->vmpeak_kb);
}

#endif /* STATS_H */
