/*
 * scalability.c — Scalability Under Contention via POSIX Semaphores
 *
 * Two modes (-m):
 *
 *   c1  Fan-in: N producers → 1 consumer through one shared semaphore.
 *       Contention is on the kernel's internal semaphore spinlock:
 *       N processes call sem_post concurrently, all fighting for the
 *       same lock.  Reports throughput (ops/s).
 *
 *   c2  Parallel pairs: N independent ping-pong pairs, each with its
 *       own two named semaphores — no shared semaphore, no shared
 *       memory.  All pairs run concurrently.  Reports per-pair latency
 *       percentiles aggregated across all pairs.
 *
 * Unlike POSIX MQs, semaphores have no directional open flags —
 * sem_open returns a sem_t* usable for both sem_post and sem_wait.
 * There is therefore no distinction between "write end" and "read end":
 * every process that has an open reference must call sem_close once.
 *
 * All semaphores are opened before any fork() to avoid startup races.
 * For C1, timing starts before the first fork so no producer work is
 * missed.
 *
 * Build note: pass -DMECHANISM='"sem_posix"' when compiling.
 *
 * Usage: scalability -m <c1|c2> [-n N] [-k ops] [-p label] [-H]
 */

#ifndef MECHANISM
#define MECHANISM "sem_posix"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <stdint.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

/* ── C1: fan-in ─────────────────────────────────────────────────────── */

#define C1_SEM "/ipc_sem_c1_items"

static void c1_producer(sem_t *items, uint32_t k)
{
    for (uint32_t i = 0; i < k; i++) {
        if (sem_post(items) != 0) { perror("sem_post c1"); exit(1); }
    }
    sem_close(items);
}

static void c1_consumer(sem_t *items, uint64_t total)
{
    for (uint64_t i = 0; i < total; i++) {
        if (sem_wait(items) != 0) { perror("sem_wait c1"); exit(1); }
    }
}

static int run_c1(int n, uint32_t k, const char *label)
{
    sem_unlink(C1_SEM);

    sem_t *items = sem_open(C1_SEM, O_CREAT | O_EXCL, 0600, 0);
    if (items == SEM_FAILED) {
        perror("sem_open c1");
        return 1;
    }

    pid_t *pids = malloc(n * sizeof(pid_t));
    if (!pids) { perror("malloc"); sem_close(items); return 1; }

    uint64_t t0 = now_ns();

    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork c1"); free(pids); return 1; }
        if (pids[i] == 0) {
            pin_to_cpu(i % num_cpus());
            c1_producer(items, k);
            exit(0);
        }
    }

    pin_to_cpu(n % num_cpus());
    c1_consumer(items, (uint64_t)n * k);
    uint64_t elapsed = now_ns() - t0;

    for (int i = 0; i < n; i++) waitpid(pids[i], NULL, 0);
    free(pids);

    uint64_t total = (uint64_t)n * k;
    double   sec   = elapsed / 1e9;
    char     bench[32];
    snprintf(bench, sizeof(bench), "fanin_n%d", n);

    result_t r = {
        .n                = total,
        .msg_size         = 0,
        .elapsed_ns       = elapsed,
        .avg_ns           = (double)elapsed / total,
        .throughput_msg_s = total / sec,
        .throughput_MB_s  = 0.0,
    };
    print_csv_row(bench, n + 1, label, &r);

    sem_close(items);
    sem_unlink(C1_SEM);
    return 0;
}

/* ── C2: parallel pairs ─────────────────────────────────────────────── */

#define C2_FWD_FMT "/ipc_sem_c2_fwd_%d"
#define C2_BWD_FMT "/ipc_sem_c2_bwd_%d"

typedef struct { sem_t *fwd, *bwd; } pair_sems_t;

static void c2_responder(sem_t *fwd, sem_t *bwd, uint64_t iters)
{
    for (uint64_t i = 0; i < iters; i++) {
        if (sem_wait(fwd) != 0)  { perror("sem_wait c2r fwd"); exit(1); }
        if (sem_post(bwd) != 0)  { perror("sem_post c2r bwd"); exit(1); }
    }
    sem_close(fwd);
    sem_close(bwd);
}

static void c2_initiator(sem_t *fwd, sem_t *bwd,
                          uint64_t iters, int pipe_wr)
{
    uint64_t *smp = alloc_samples(iters);

    for (uint64_t i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        if (sem_post(fwd) != 0)  { perror("sem_post c2i fwd"); exit(1); }
        if (sem_wait(bwd) != 0)  { perror("sem_wait c2i bwd"); exit(1); }
        smp[i] = (now_ns() - t0) / 2;
    }
    sem_close(fwd);
    sem_close(bwd);

    size_t      remaining = iters * sizeof(uint64_t);
    const char *ptr       = (const char *)smp;
    while (remaining > 0) {
        ssize_t w = write(pipe_wr, ptr, remaining);
        if (w <= 0) { perror("pipe write c2"); exit(1); }
        ptr       += w;
        remaining -= (size_t)w;
    }
    free(smp);
    close(pipe_wr);
}

static int run_c2(int n, uint64_t iters, const char *label)
{
    char fn[64], bn[64];

    pair_sems_t *sems      = malloc(n * sizeof(pair_sems_t));
    int        (*pipes)[2] = malloc(n * sizeof(*pipes));
    pid_t       *resp_pids = malloc(n * sizeof(pid_t));
    pid_t       *init_pids = malloc(n * sizeof(pid_t));
    if (!sems || !pipes || !resp_pids || !init_pids) {
        perror("malloc c2");
        free(sems); free(pipes); free(resp_pids); free(init_pids);
        return 1;
    }

    /* Open all 2*n semaphores before any fork() */
    for (int p = 0; p < n; p++) {
        snprintf(fn, sizeof(fn), C2_FWD_FMT, p);
        snprintf(bn, sizeof(bn), C2_BWD_FMT, p);
        sem_unlink(fn);
        sem_unlink(bn);

        sems[p].fwd = sem_open(fn, O_CREAT | O_EXCL, 0600, 0);
        sems[p].bwd = sem_open(bn, O_CREAT | O_EXCL, 0600, 0);
        if (sems[p].fwd == SEM_FAILED || sems[p].bwd == SEM_FAILED) {
            perror("sem_open c2 pair");
            for (int q = 0; q <= p; q++) {
                if (sems[q].fwd != SEM_FAILED) sem_close(sems[q].fwd);
                if (sems[q].bwd != SEM_FAILED) sem_close(sems[q].bwd);
                snprintf(fn, sizeof(fn), C2_FWD_FMT, q);
                snprintf(bn, sizeof(bn), C2_BWD_FMT, q);
                sem_unlink(fn); sem_unlink(bn);
            }
            free(sems); free(pipes); free(resp_pids); free(init_pids);
            return 1;
        }
    }

    for (int p = 0; p < n; p++) {
        if (pipe(pipes[p]) != 0) {
            perror("pipe c2");
            for (int q = 0; q < n; q++) {
                sem_close(sems[q].fwd); sem_close(sems[q].bwd);
                snprintf(fn, sizeof(fn), C2_FWD_FMT, q);
                snprintf(bn, sizeof(bn), C2_BWD_FMT, q);
                sem_unlink(fn); sem_unlink(bn);
            }
            free(sems); free(pipes); free(resp_pids); free(init_pids);
            return 1;
        }
    }

    /*
     * Fork all 2*n children — all pairs then run concurrently.
     *
     * Default 64 KB pipe buffer is safe for <= 8000 iterations.
     * For larger runs raise with:
     *   fcntl(pipes[p][1], F_SETPIPE_SZ, iters * sizeof(uint64_t))
     */
    for (int p = 0; p < n; p++) {

        resp_pids[p] = fork();
        if (resp_pids[p] < 0) { perror("fork resp c2"); return 1; }
        if (resp_pids[p] == 0) {
            for (int q = 0; q < n; q++) {
                if (q != p) { sem_close(sems[q].fwd); sem_close(sems[q].bwd); }
                close(pipes[q][0]);
                close(pipes[q][1]);
            }
            pin_to_cpu((2 * p + 1) % num_cpus());
            c2_responder(sems[p].fwd, sems[p].bwd, iters);
            exit(0);
        }

        init_pids[p] = fork();
        if (init_pids[p] < 0) { perror("fork init c2"); return 1; }
        if (init_pids[p] == 0) {
            for (int q = 0; q < n; q++) {
                if (q != p) { sem_close(sems[q].fwd); sem_close(sems[q].bwd); }
                close(pipes[q][0]);
                if (q != p) close(pipes[q][1]);
            }
            pin_to_cpu((2 * p) % num_cpus());
            c2_initiator(sems[p].fwd, sems[p].bwd, iters, pipes[p][1]);
            exit(0);
        }
    }

    /* Parent: close all semaphore refs and pipe write ends */
    for (int p = 0; p < n; p++) {
        sem_close(sems[p].fwd);
        sem_close(sems[p].bwd);
        close(pipes[p][1]);
    }

    /*
     * Drain pipes sequentially — initiators run concurrently so
     * their data buffers while parent reads earlier pipes.
     * Elapsed covers pipe-drain time; throughput is meaningless here.
     * Only latency percentiles are valid for C2.
     */
    uint64_t  total = (uint64_t)n * iters;
    uint64_t *flat  = malloc(total * sizeof(uint64_t));
    if (!flat) { perror("malloc flat c2"); return 1; }

    for (int p = 0; p < n; p++) {
        char  *ptr       = (char *)(flat + (uint64_t)p * iters);
        size_t remaining = iters * sizeof(uint64_t);
        while (remaining > 0) {
            ssize_t got = read(pipes[p][0], ptr, remaining);
            if (got <= 0) break;
            ptr       += got;
            remaining -= (size_t)got;
        }
        close(pipes[p][0]);
    }

    for (int p = 0; p < n; p++) {
        waitpid(resp_pids[p], NULL, 0);
        waitpid(init_pids[p], NULL, 0);
    }

    char bench[32];
    snprintf(bench, sizeof(bench), "pairs_n%d", n);

    result_t r = { .samples = flat, .n = total,
                   .msg_size = 0, .elapsed_ns = 0 };
    compute_stats(&r);
    r.throughput_msg_s = 0;
    r.throughput_MB_s  = 0;
    print_csv_row(bench, n * 2, label, &r);

    for (int p = 0; p < n; p++) {
        snprintf(fn, sizeof(fn), C2_FWD_FMT, p);
        snprintf(bn, sizeof(bn), C2_BWD_FMT, p);
        sem_unlink(fn);
        sem_unlink(bn);
    }

    free(flat);
    free(sems);
    free(pipes);
    free(resp_pids);
    free(init_pids);
    return 0;
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    char        mode[4] = "";
    int         n       = 4;
    uint32_t    k       = 5000;
    const char *label   = "unspecified";
    int         opt;

    while ((opt = getopt(argc, argv, "m:n:k:p:H")) != -1) {
        switch (opt) {
        case 'm': strncpy(mode, optarg, 3);                   break;
        case 'n': n = (int)strtol(optarg, NULL, 10);          break;
        case 'k': k = (uint32_t)strtoul(optarg, NULL, 10);    break;
        case 'p': label = optarg;                              break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr,
                    "usage: scalability -m <c1|c2> [-n N] [-k K] "
                    "[-p label] [-H]\n");
            return 1;
        }
    }

    if      (strcmp(mode, "c1") == 0) return run_c1(n, k, label);
    else if (strcmp(mode, "c2") == 0) return run_c2(n, (uint64_t)k, label);
    else {
        fprintf(stderr, "error: -m is required, use c1 or c2\n");
        return 1;
    }
}
