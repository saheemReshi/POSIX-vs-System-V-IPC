#ifndef TIMING_H
#define TIMING_H

/*
 * timing.h — shared timing, result, and CSV helpers
 *
 * The mechanism string written into every CSV row is controlled by the
 * MECHANISM preprocessor macro.  Each sub-suite passes its own value:
 *
 *   mq_posix/:   -DMECHANISM='"mq_posix"'
 *   sem_posix/:  -DMECHANISM='"sem_posix"'
 *
 * If MECHANISM is not defined at compile time, a loud placeholder is
 * used so the omission is immediately visible in the CSV output.
 */
#ifndef MECHANISM
#define MECHANISM "unknown_mechanism"
#endif

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

typedef struct {
    uint64_t *samples;    /* per-iteration latency (ns), NULL for throughput */
    uint64_t  n;
    size_t    msg_size;
    uint64_t  elapsed_ns;
    double    avg_ns, p50_ns, p99_ns, min_ns, max_ns;
    double    throughput_msg_s, throughput_MB_s;
} result_t;

/* Must be static inline to avoid duplicate-symbol errors when this
 * header is included by multiple translation units in the same build. */
static inline int _cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static inline void compute_stats(result_t *r)
{
    if (!r->samples || r->n == 0) return;
    uint64_t *s = malloc(r->n * sizeof(uint64_t));
    if (!s) { perror("malloc"); exit(1); }
    memcpy(s, r->samples, r->n * sizeof(uint64_t));
    qsort(s, r->n, sizeof(uint64_t), _cmp_u64);

    double sum = 0;
    for (uint64_t i = 0; i < r->n; i++) sum += s[i];
    r->avg_ns = sum / r->n;
    r->min_ns = s[0];
    r->max_ns = s[r->n - 1];
    r->p50_ns = s[r->n * 50 / 100];
    r->p99_ns = s[r->n * 99 / 100];

    double sec = r->elapsed_ns / 1e9;
    r->throughput_msg_s = r->n / sec;
    r->throughput_MB_s  = (r->n * r->msg_size) / sec / (1024.0 * 1024.0);
    free(s);
}

static inline void print_csv_header(void)
{
    printf("mechanism,benchmark,n_procs,msg_size_bytes,placement,"
           "iterations,avg_ns,p50_ns,p99_ns,min_ns,max_ns,"
           "throughput_msg_s,throughput_MB_s\n");
}

/*
 * print_csv_row — write one result line to stdout.
 *
 * The first CSV field is the MECHANISM macro value, set at compile
 * time via -DMECHANISM='"mq_posix"' or -DMECHANISM='"sem_posix"'.
 * All other fields are identical across both suites.
 */
static inline void print_csv_row(const char *bench, int n_procs,
                                 const char *placement, const result_t *r)
{
    printf("%s,%s,%d,%zu,%s,"
           "%" PRIu64 ",%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.3f\n",
           MECHANISM,
           bench, n_procs, r->msg_size, placement,
           r->n,
           r->avg_ns, r->p50_ns, r->p99_ns, r->min_ns, r->max_ns,
           r->throughput_msg_s, r->throughput_MB_s);
}

#endif /* TIMING_H */
