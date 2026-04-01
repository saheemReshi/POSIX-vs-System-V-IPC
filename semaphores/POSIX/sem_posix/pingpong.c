/*
 * pingpong.c — Ping-Pong Latency via POSIX Semaphores
 *
 * Two processes, two named semaphores:
 *
 *   P1 --[sem_fwd]--> P2
 *   P1 <--[sem_bwd]-- P2
 *
 * P1 posts sem_fwd and blocks on sem_bwd.
 * P2 waits on sem_fwd and immediately posts sem_bwd.
 * RTT recorded per iteration; one-way latency = RTT / 2.
 *
 * Semaphores carry no data; msg_size is always 0 and
 * throughput_MB_s is always 0.  throughput_msg_s reports
 * round-trips per second, which is the meaningful metric
 * for a pure signalling channel.
 *
 * Both semaphores are opened before fork() so neither process
 * races at startup and the first sample is not inflated.
 *
 * Build note: timing.h should emit MECHANISM via a preprocessor
 * macro.  Pass -DMECHANISM='"sem_posix"' when compiling this file.
 *
 * Usage: pingpong [-n iters] [-c cpu0,cpu1] [-p label] [-H]
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

#define SEM_FWD "/ipc_sem_pp_fwd"
#define SEM_BWD "/ipc_sem_pp_bwd"

/* P1: initiator — posts fwd, waits bwd, records RTT */
static void run_p1(sem_t *fwd, sem_t *bwd,
                   uint64_t iters, uint64_t *samples)
{
    for (uint64_t i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        if (sem_post(fwd) != 0)  { perror("sem_post p1 fwd"); exit(1); }
        if (sem_wait(bwd) != 0)  { perror("sem_wait p1 bwd"); exit(1); }
        samples[i] = now_ns() - t0;   /* RTT */
    }
}

/* P2: responder — waits fwd, posts bwd */
static void run_p2(sem_t *fwd, sem_t *bwd, uint64_t iters)
{
    for (uint64_t i = 0; i < iters; i++) {
        if (sem_wait(fwd) != 0)  { perror("sem_wait p2 fwd"); exit(1); }
        if (sem_post(bwd) != 0)  { perror("sem_post p2 bwd"); exit(1); }
    }
}

int main(int argc, char *argv[])
{
    uint64_t    iters = 10000;
    int         cpu0  = -1, cpu1 = -1;
    const char *label = "unspecified";
    int         opt;

    while ((opt = getopt(argc, argv, "n:c:p:H")) != -1) {
        switch (opt) {
        case 'n': iters = strtoull(optarg, NULL, 10);     break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1); break;
        case 'p': label = optarg;                         break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr,
                    "usage: pingpong [-n iters] [-c cpu0,cpu1] "
                    "[-p label] [-H]\n");
            return 1;
        }
    }

    /* Tear down any stale semaphores from a previous crashed run */
    sem_unlink(SEM_FWD);
    sem_unlink(SEM_BWD);

    /* Both semaphores start at 0:
     *   fwd  blocks P2's wait until P1 posts
     *   bwd  blocks P1's wait until P2 echoes
     * Opened before fork() — no startup race, no open-time
     * latency leaking into the first measurement sample.      */
    sem_t *fwd = sem_open(SEM_FWD, O_CREAT | O_EXCL, 0600, 0);
    sem_t *bwd = sem_open(SEM_BWD, O_CREAT | O_EXCL, 0600, 0);
    if (fwd == SEM_FAILED || bwd == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    uint64_t *samples = alloc_samples(iters);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        /* Child is P2 (responder) */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_p2(fwd, bwd, iters);
        sem_close(fwd);
        sem_close(bwd);
        exit(0);
    }

    /* Parent is P1 (initiator) */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    uint64_t t0 = now_ns();
    run_p1(fwd, bwd, iters, samples);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);

    /* RTT → one-way latency */
    for (uint64_t i = 0; i < iters; i++) samples[i] /= 2;

    result_t r = { .samples    = samples,
                   .n          = iters,
                   .msg_size   = 0,       /* semaphores carry no payload */
                   .elapsed_ns = elapsed };
    compute_stats(&r);

    double sec = elapsed / 1e9;
    r.throughput_msg_s = iters / sec;   /* round-trips / sec */
    r.throughput_MB_s  = 0.0;           /* no data transferred */
    print_csv_row("pingpong", 2, label, &r);

    sem_close(fwd);
    sem_close(bwd);
    sem_unlink(SEM_FWD);
    sem_unlink(SEM_BWD);
    free(samples);
    return 0;
}
