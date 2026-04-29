/*
 * shm_pairs.c — Parallel-Pairs Scalability via POSIX Shared Memory
 *
 * N independent shared-memory ping-pong pairs run concurrently.
 * Each pair owns:
 *   - one shm region of size 2*msz (ping + pong slots)
 *   - two named POSIX semaphores (fwd, bwd)
 *   - two processes (initiator, responder)
 *
 * Per-iteration RTT samples are collected by every initiator and shipped
 * to the parent over a per-pair pipe.  The parent aggregates samples
 * across all pairs into one stream and computes p50 / p95 / p99 / p99.9.
 *
 * Mirrors mq_posix scalability c2 (parallel pairs) so the row layout is
 * directly comparable.
 *
 * Usage: shm_pairs [-n N] [-k iters] [-s bytes] [-p label] [-r run_id] [-H]
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

#define SHM_FMT  "/ipc_shm_pairs_%d"
#define FWD_FMT  "/ipc_shm_pairs_fwd_%d"
#define BWD_FMT  "/ipc_shm_pairs_bwd_%d"

#define WARMUP_FRAC 0.05
#define WARMUP_MAX  5000

typedef struct {
    char         *shm_name;
    char         *fwd_name;
    char         *bwd_name;
    int           shm_fd;
    volatile char *shm;
    sem_t        *fwd;
    sem_t        *bwd;
} pair_t;

static void responder(pair_t *p, size_t msz, uint64_t iters)
{
    volatile char *ping = p->shm;
    volatile char *pong = p->shm + msz;

    for (uint64_t i = 0; i < iters; i++) {
        while (sem_wait(p->fwd) != 0) if (errno != EINTR) exit(1);
        memcpy((void *)pong, (const void *)ping, msz);
        if (sem_post(p->bwd) != 0) exit(1);
    }
}

static void initiator(pair_t *p, size_t msz, uint64_t iters,
                       int pipe_wr, uint64_t warmup)
{
    volatile char *ping = p->shm;
    volatile char *pong = p->shm + msz;

    /* Warmup outside timing */
    for (uint64_t i = 0; i < warmup; i++) {
        memset((void *)ping, (int)(i & 0xFF), msz);
        if (sem_post(p->fwd) != 0) exit(1);
        while (sem_wait(p->bwd) != 0) if (errno != EINTR) exit(1);
        (void)pong[0];
    }

    uint64_t *smp = alloc_samples(iters);
    for (uint64_t i = 0; i < iters; i++) {
        memset((void *)ping, (int)(i & 0xFF), msz);
        uint64_t t0 = now_ns();
        if (sem_post(p->fwd) != 0) exit(1);
        while (sem_wait(p->bwd) != 0) if (errno != EINTR) exit(1);
        smp[i] = (now_ns() - t0) / 2;   /* one-way */
        (void)pong[0];
    }

    size_t remaining = iters * sizeof(uint64_t);
    const char *ptr = (const char *)smp;
    while (remaining > 0) {
        ssize_t w = write(pipe_wr, ptr, remaining);
        if (w <= 0) { perror("pipe write"); exit(1); }
        ptr += w; remaining -= (size_t)w;
    }
    free(smp);
    close(pipe_wr);
}

static int run_pairs(int n, uint64_t iters, size_t msz,
                      const char *label, int run_id)
{
    if (n < 1 || iters == 0 || msz == 0) {
        fprintf(stderr, "usage: shm_pairs -n N -k K -s bytes\n");
        return 1;
    }

    uint64_t warmup = (uint64_t)((double)iters * WARMUP_FRAC);
    if (warmup > WARMUP_MAX) warmup = WARMUP_MAX;

    pair_t *pairs = calloc((size_t)n, sizeof(pair_t));
    if (!pairs) { perror("calloc"); return 1; }
    int (*pipes)[2] = malloc((size_t)n * sizeof(*pipes));
    pid_t *resp_pids = malloc((size_t)n * sizeof(pid_t));
    pid_t *init_pids = malloc((size_t)n * sizeof(pid_t));
    if (!pipes || !resp_pids || !init_pids) {
        perror("malloc"); return 1;
    }

    size_t shm_size = 2 * msz;

    for (int p = 0; p < n; p++) {
        char buf[64];
        snprintf(buf, sizeof(buf), SHM_FMT, p); pairs[p].shm_name = strdup(buf);
        snprintf(buf, sizeof(buf), FWD_FMT, p); pairs[p].fwd_name = strdup(buf);
        snprintf(buf, sizeof(buf), BWD_FMT, p); pairs[p].bwd_name = strdup(buf);

        shm_unlink(pairs[p].shm_name);
        sem_unlink(pairs[p].fwd_name);
        sem_unlink(pairs[p].bwd_name);

        pairs[p].shm_fd = shm_open(pairs[p].shm_name,
                                    O_CREAT | O_RDWR, 0600);
        if (pairs[p].shm_fd < 0) { perror("shm_open"); return 1; }
        if (ftruncate(pairs[p].shm_fd, (off_t)shm_size) != 0) {
            perror("ftruncate"); return 1;
        }
        pairs[p].shm = mmap(NULL, shm_size,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             pairs[p].shm_fd, 0);
        if (pairs[p].shm == MAP_FAILED) { perror("mmap"); return 1; }

        pairs[p].fwd = sem_open(pairs[p].fwd_name,
                                 O_CREAT | O_EXCL, 0600, 0);
        pairs[p].bwd = sem_open(pairs[p].bwd_name,
                                 O_CREAT | O_EXCL, 0600, 0);
        if (pairs[p].fwd == SEM_FAILED || pairs[p].bwd == SEM_FAILED) {
            perror("sem_open"); return 1;
        }

        if (pipe(pipes[p]) != 0) { perror("pipe"); return 1; }
    }

    int n_cpus = num_cpus();
    uint64_t t0 = now_ns();

    for (int p = 0; p < n; p++) {
        resp_pids[p] = fork();
        if (resp_pids[p] < 0) { perror("fork resp"); return 1; }
        if (resp_pids[p] == 0) {
            for (int q = 0; q < n; q++) {
                if (q != p) close(pipes[q][0]);
                if (q != p) close(pipes[q][1]);
            }
            close(pipes[p][0]); close(pipes[p][1]);
            pin_to_cpu((2 * p + 1) % n_cpus);
            responder(&pairs[p], msz, warmup + iters);
            exit(0);
        }

        init_pids[p] = fork();
        if (init_pids[p] < 0) { perror("fork init"); return 1; }
        if (init_pids[p] == 0) {
            for (int q = 0; q < n; q++) {
                if (q != p) { close(pipes[q][0]); close(pipes[q][1]); }
            }
            close(pipes[p][0]);   /* keep write end only */
            pin_to_cpu((2 * p) % n_cpus);
            initiator(&pairs[p], msz, iters, pipes[p][1], warmup);
            exit(0);
        }
    }

    /* Parent: close all pipe write ends, drain reads */
    for (int p = 0; p < n; p++) close(pipes[p][1]);

    uint64_t total = (uint64_t)n * iters;
    uint64_t *flat = malloc(total * sizeof(uint64_t));
    if (!flat) { perror("malloc flat"); return 1; }

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
    uint64_t elapsed = now_ns() - t0;

    for (int p = 0; p < n; p++) {
        waitpid(resp_pids[p], NULL, 0);
        waitpid(init_pids[p], NULL, 0);
    }

    char bench[32];
    snprintf(bench, sizeof(bench), "pairs_n%d", n);
    result_t r = {
        .samples    = flat,
        .n          = total,
        .msg_size   = msz,
        .elapsed_ns = elapsed,
        .run_id     = run_id,
    };
    compute_stats(&r);
    r.throughput_msg_s = 0;
    r.throughput_MB_s  = 0;
    print_csv_row_mech("shm_posix", bench, n * 2, label, &r);

    /* Cleanup */
    for (int p = 0; p < n; p++) {
        sem_close(pairs[p].fwd); sem_unlink(pairs[p].fwd_name);
        sem_close(pairs[p].bwd); sem_unlink(pairs[p].bwd_name);
        munmap((void *)pairs[p].shm, shm_size);
        close(pairs[p].shm_fd);
        shm_unlink(pairs[p].shm_name);
        free(pairs[p].shm_name);
        free(pairs[p].fwd_name);
        free(pairs[p].bwd_name);
    }
    free(pairs); free(pipes); free(resp_pids); free(init_pids); free(flat);
    return 0;
}

int main(int argc, char *argv[])
{
    int          n      = 4;
    uint64_t     iters  = 5000;
    size_t       msz    = 64;
    const char  *label  = "unspecified";
    int          run_id = 0;
    int          opt;

    while ((opt = getopt(argc, argv, "n:k:s:p:r:H")) != -1) {
        switch (opt) {
        case 'n': n      = (int)strtol(optarg, NULL, 10);          break;
        case 'k': iters  = strtoull(optarg, NULL, 10);              break;
        case 's': msz    = strtoul(optarg, NULL, 10);                break;
        case 'p': label  = optarg;                                    break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10);            break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: shm_pairs [-n N] [-k K] [-s bytes] "
                            "[-p label] [-r run_id] [-H]\n");
            return 1;
        }
    }

    return run_pairs(n, iters, msz, label, run_id);
}
