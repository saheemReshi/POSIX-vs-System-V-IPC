#ifndef TIMING_H
#define TIMING_H

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* ── High-resolution monotonic clock ───────────────────────────────────── */

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* ── Result structure ───────────────────────────────────────────────────── */
/*
 * All latency fields are in nanoseconds.
 * Throughput fields are raw; the caller corrects them for bidirectional
 * benchmarks (e.g. ping-pong) after compute_stats() returns.
 *
 * mem_delta_kb: VmRSS change in KB across the benchmark, 0 if not measured.
 * run_id:       which repetition (1..N) this row belongs to; 0 = single run.
 */
typedef struct {
    /* raw per-iteration samples (ns); NULL for throughput-only runs */
    uint64_t *samples;
    uint64_t  n;
    size_t    msg_size;
    uint64_t  elapsed_ns;

    /* latency percentiles (ns) */
    double avg_ns;
    double p50_ns;
    double p95_ns;   /* IEEE extension */
    double p99_ns;
    double p999_ns;  /* IEEE extension: p99.9 */
    double min_ns;
    double max_ns;

    /* throughput */
    double throughput_msg_s;
    double throughput_MB_s;

    /* memory footprint delta (VmRSS KB) */
    long mem_delta_kb;

    /* repetition index for multi-run orchestration (0 = single run) */
    int run_id;
} result_t;

/* ── Comparator for qsort ───────────────────────────────────────────────── */

static inline int _cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* ── Percentile helper ───────────────────────────────────────────────────
 *
 * Nearest-rank method. For p99.9 you need n >= 1000 for a non-saturated
 * estimate; with 50 000 iterations this is comfortably satisfied.
 */
static inline double _pct(const uint64_t *sorted, uint64_t n, double pct)
{
    if (n == 0) return 0.0;
    uint64_t idx = (uint64_t)(pct / 100.0 * (double)n);
    if (idx >= n) idx = n - 1;
    return (double)sorted[idx];
}

/* ── compute_stats ──────────────────────────────────────────────────────── */
/*
 * Sorts a temporary copy of r->samples, computes all percentile and
 * throughput fields. Safe to call multiple times.
 */
static inline void compute_stats(result_t *r)
{
    if (!r->samples || r->n == 0) return;

    uint64_t *s = malloc(r->n * sizeof(uint64_t));
    if (!s) { perror("malloc compute_stats"); exit(1); }
    memcpy(s, r->samples, r->n * sizeof(uint64_t));
    qsort(s, r->n, sizeof(uint64_t), _cmp_u64);

    double sum = 0.0;
    for (uint64_t i = 0; i < r->n; i++) sum += (double)s[i];
    r->avg_ns  = sum / (double)r->n;
    r->min_ns  = (double)s[0];
    r->max_ns  = (double)s[r->n - 1];
    r->p50_ns  = _pct(s, r->n, 50.0);
    r->p95_ns  = _pct(s, r->n, 95.0);
    r->p99_ns  = _pct(s, r->n, 99.0);
    r->p999_ns = _pct(s, r->n, 99.9);

    double sec = (double)r->elapsed_ns / 1e9;
    r->throughput_msg_s = (double)r->n / sec;
    r->throughput_MB_s  = ((double)r->n * (double)r->msg_size)
                           / sec / (1024.0 * 1024.0);

    free(s);
}

/* ── CSV I/O ────────────────────────────────────────────────────────────── */
/*
 * Extended IEEE column layout:
 *   mechanism, benchmark, n_procs, msg_size_bytes, placement, run_id,
 *   iterations,
 *   avg_ns, p50_ns, p95_ns, p99_ns, p999_ns, min_ns, max_ns,
 *   throughput_msg_s, throughput_MB_s,
 *   mem_delta_kb
 */
static inline void print_csv_header(void)
{
    printf("mechanism,benchmark,n_procs,msg_size_bytes,placement,run_id,"
           "iterations,"
           "avg_ns,p50_ns,p95_ns,p99_ns,p999_ns,min_ns,max_ns,"
           "throughput_msg_s,throughput_MB_s,"
           "mem_delta_kb\n");
}

/* Core row printer — mechanism passed explicitly for multi-mechanism suites */
static inline void print_csv_row_mech(const char *mechanism,
                                       const char *bench, int n_procs,
                                       const char *placement,
                                       const result_t *r)
{
    printf("%s,%s,%d,%zu,%s,%d,"
           "%" PRIu64 ","
           "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
           "%.0f,%.3f,"
           "%ld\n",
           mechanism, bench, n_procs, r->msg_size, placement, r->run_id,
           r->n,
           r->avg_ns, r->p50_ns, r->p95_ns, r->p99_ns, r->p999_ns,
           r->min_ns, r->max_ns,
           r->throughput_msg_s, r->throughput_MB_s,
           r->mem_delta_kb);
}

/* Convenience wrapper for System V MQ benchmarks */
static inline void print_csv_row(const char *bench, int n_procs,
                                  const char *placement, const result_t *r)
{
    print_csv_row_mech("mq_sysv", bench, n_procs, placement, r);
}

#endif /* TIMING_H */
