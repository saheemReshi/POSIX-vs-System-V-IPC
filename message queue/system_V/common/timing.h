#ifndef TIMING_H
#define TIMING_H
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

static inline void print_csv_row(const char *bench, int n_procs,
                                 const char *placement, const result_t *r)
{
    /* "mq_sysv" identifies System V results in the combined CSV */
    printf("mq_sysv,%s,%d,%zu,%s,"
           "%" PRIu64 ",%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.3f\n",
           bench, n_procs, r->msg_size, placement,
           r->n,
           r->avg_ns, r->p50_ns, r->p99_ns, r->min_ns, r->max_ns,
           r->throughput_msg_s, r->throughput_MB_s);
}

#endif /* TIMING_H */
