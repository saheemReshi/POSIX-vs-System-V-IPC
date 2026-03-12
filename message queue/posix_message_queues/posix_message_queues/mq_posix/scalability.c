/*
 * scalability.c — Scalability Under Contention via POSIX Message Queues
 *
 * Two modes (-m):
 *
 *   c1  Fan-in: N producers → 1 consumer, all through one shared queue.
 *       Contention is on the kernel's internal queue lock.
 *       Reports throughput (msgs/s, MB/s).
 *
 *   c2  Parallel pairs: N independent ping-pong pairs, all running at once.
 *       Each pair has its own two queues — no shared queue, no shared memory.
 *       Reports per-pair latency percentiles aggregated across all pairs.
 *
 * All queue descriptors are opened before fork() to avoid startup races.
 * Timing starts before any fork so no producer work is missed.
 *
 * Usage: scalability -m <c1|c2> [-n N] [-k msgs] [-s bytes] [-p label] [-H]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/wait.h>
#include <stdint.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

/* ── C1: fan-in ────────────────────────────────────────────────────── */

#define C1_Q "/ipc_scale_c1"

static void c1_producer(mqd_t q, uint32_t k, size_t msz, int id)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    memset(buf, id & 0xFF, msz);

    for (uint32_t i = 0; i < k; i++) {
        buf[0] = (char)(i & 0xFF);
        if (mq_send(q, buf, msz, 0) != 0) { perror("mq_send c1"); exit(1); }
    }
    free(buf);
    mq_close(q);
}

static void c1_consumer(mqd_t q, uint64_t total, size_t msz)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }

    for (uint64_t i = 0; i < total; i++)
        if (mq_receive(q, buf, msz, NULL) < 0) { perror("mq_receive c1"); exit(1); }

    free(buf);
    mq_close(q);
}

static int run_c1(int n, uint32_t k, size_t msz, const char *label)
{
    /* Queue depth: ideally n*k so producers never block, capped at 1024
     * (kernel default msg_max). If your kernel allows more, raise it with:
     *   sudo sysctl fs.mqueue.msg_max=<n*k>                              */
    long depth = (long)n * k;
    if (depth > 1024) depth = 1024;

    struct mq_attr attr = { .mq_maxmsg = depth, .mq_msgsize = (long)msz };
    mq_unlink(C1_Q);

    /* Open one write descriptor per producer + one read descriptor for
     * the consumer, all before any fork() so there is no startup race. */
    mqd_t *wq = malloc(n * sizeof(mqd_t));
    if (!wq) { perror("malloc"); return 1; }

    /* First open creates the queue */
    wq[0] = mq_open(C1_Q, O_CREAT | O_WRONLY, 0600, &attr);
    if (wq[0] == (mqd_t)-1) {
        perror("mq_open c1 create");
        fprintf(stderr, "try: sudo sysctl fs.mqueue.msg_max=%ld "
                        "fs.mqueue.msgsize_max=%zu\n", depth, msz);
        free(wq); return 1;
    }
    for (int i = 1; i < n; i++) {
        wq[i] = mq_open(C1_Q, O_WRONLY);
        if (wq[i] == (mqd_t)-1) { perror("mq_open c1 wq"); free(wq); return 1; }
    }
    mqd_t rq = mq_open(C1_Q, O_RDONLY);
    if (rq == (mqd_t)-1) { perror("mq_open c1 rq"); free(wq); return 1; }

    /* Start timing before forks so we capture all producer work */
    uint64_t t0 = now_ns();

    pid_t *pids = malloc(n * sizeof(pid_t));
    if (!pids) { perror("malloc"); free(wq); return 1; }

    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); free(wq); free(pids); return 1; }
        if (pids[i] == 0) {
            /* Close all descriptors except this producer's own write end */
            for (int j = 0; j < n; j++) if (j != i) mq_close(wq[j]);
            mq_close(rq);
            pin_to_cpu(i % num_cpus());
            c1_producer(wq[i], k, msz, i);
            exit(0);
        }
        /* Parent closes the write end it just handed to a child */
        mq_close(wq[i]);
    }
    free(wq);

    /* Parent is the consumer */
    pin_to_cpu(n % num_cpus());
    c1_consumer(rq, (uint64_t)n * k, msz);
    uint64_t elapsed = now_ns() - t0;

    for (int i = 0; i < n; i++) waitpid(pids[i], NULL, 0);
    free(pids);

    uint64_t total = (uint64_t)n * k;
    double sec = elapsed / 1e9;
    char bench[32];
    snprintf(bench, sizeof(bench), "fanin_n%d", n);
    result_t r = {
        .n                = total,
        .msg_size         = msz,
        .elapsed_ns       = elapsed,
        .avg_ns           = (double)elapsed / total,
        .throughput_msg_s = total / sec,
        .throughput_MB_s  = (total * msz) / sec / (1024.0 * 1024.0),
    };
    print_csv_row(bench, n + 1, label, &r);

    mq_unlink(C1_Q);
    return 0;
}

/* ── C2: parallel pairs ─────────────────────────────────────────────── */

#define C2_FWD "/ipc_scale_c2_fwd_%d"
#define C2_BWD "/ipc_scale_c2_bwd_%d"

/* Descriptors for one pair, opened before fork */
typedef struct { mqd_t i_fwd, i_bwd, r_fwd, r_bwd; } pair_fds_t;

static void c2_responder(mqd_t fwd, mqd_t bwd, uint64_t iters, size_t msz)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }

    for (uint64_t i = 0; i < iters; i++) {
        if (mq_receive(fwd, buf, msz, NULL) < 0) { perror("mq_receive c2 resp"); exit(1); }
        if (mq_send(bwd, buf, msz, 0) != 0)      { perror("mq_send c2 resp");    exit(1); }
    }
    free(buf);
    mq_close(fwd); mq_close(bwd);
}

static void c2_initiator(mqd_t fwd, mqd_t bwd, uint64_t iters,
                          size_t msz, int pipe_wr)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    uint64_t *smp = alloc_samples(iters);

    for (uint64_t i = 0; i < iters; i++) {
        memset(buf, (int)(i & 0xFF), msz);
        uint64_t t0 = now_ns();
        if (mq_send(fwd, buf, msz, 0) != 0)      { perror("mq_send c2 init");    exit(1); }
        if (mq_receive(bwd, buf, msz, NULL) < 0)  { perror("mq_receive c2 init"); exit(1); }
        smp[i] = (now_ns() - t0) / 2;  /* one-way */
    }
    free(buf);
    mq_close(fwd); mq_close(bwd);

    /* Send samples to parent via pipe */
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

static int run_c2(int n, uint64_t iters, size_t msz, const char *label)
{
    struct mq_attr attr = { .mq_maxmsg = 1, .mq_msgsize = (long)msz };
    char fn[64], bn[64];

    /* Open all descriptors for all pairs before any fork() */
    pair_fds_t *fds = malloc(n * sizeof(pair_fds_t));
    if (!fds) { perror("malloc"); return 1; }

    for (int p = 0; p < n; p++) {
        snprintf(fn, sizeof(fn), C2_FWD, p);
        snprintf(bn, sizeof(bn), C2_BWD, p);
        mq_unlink(fn); mq_unlink(bn);

        fds[p].i_fwd = mq_open(fn, O_CREAT | O_WRONLY, 0600, &attr);
        fds[p].r_bwd = mq_open(bn, O_CREAT | O_WRONLY, 0600, &attr);
        fds[p].r_fwd = mq_open(fn, O_RDONLY);
        fds[p].i_bwd = mq_open(bn, O_RDONLY);
        if (fds[p].i_fwd==(mqd_t)-1 || fds[p].r_bwd==(mqd_t)-1 ||
            fds[p].r_fwd==(mqd_t)-1 || fds[p].i_bwd==(mqd_t)-1) {
            perror("mq_open c2");
            fprintf(stderr, "try: sudo sysctl fs.mqueue.msgsize_max=%zu\n", msz);
            free(fds); return 1;
        }
    }

    /* One pipe per pair for returning samples to parent */
    int (*pipes)[2] = malloc(n * sizeof(*pipes));
    pid_t *resp_pids = malloc(n * sizeof(pid_t));
    pid_t *init_pids = malloc(n * sizeof(pid_t));
    if (!pipes || !resp_pids || !init_pids) { perror("malloc"); return 1; }

    for (int p = 0; p < n; p++) {
        if (pipe(pipes[p]) != 0) { perror("pipe"); return 1; }
    }

    /* Fork all children — after this point all 2*N pairs run concurrently */
    for (int p = 0; p < n; p++) {
        resp_pids[p] = fork();
        if (resp_pids[p] < 0) { perror("fork resp"); return 1; }
        if (resp_pids[p] == 0) {
            /* Close every descriptor except this pair's responder ends */
            for (int q = 0; q < n; q++) {
                if (q != p) {
                    mq_close(fds[q].i_fwd); mq_close(fds[q].r_bwd);
                    mq_close(fds[q].r_fwd); mq_close(fds[q].i_bwd);
                }
                close(pipes[q][0]); close(pipes[q][1]);
            }
            mq_close(fds[p].i_fwd); mq_close(fds[p].i_bwd);
            pin_to_cpu((2*p + 1) % num_cpus());
            c2_responder(fds[p].r_fwd, fds[p].r_bwd, iters, msz);
            exit(0);
        }

        init_pids[p] = fork();
        if (init_pids[p] < 0) { perror("fork init"); return 1; }
        if (init_pids[p] == 0) {
            /* Close every descriptor except this pair's initiator ends */
            for (int q = 0; q < n; q++) {
                if (q != p) {
                    mq_close(fds[q].i_fwd); mq_close(fds[q].r_bwd);
                    mq_close(fds[q].r_fwd); mq_close(fds[q].i_bwd);
                }
                if (q != p) { close(pipes[q][0]); close(pipes[q][1]); }
                else         { close(pipes[q][0]); } /* close read end only */
            }
            mq_close(fds[p].r_fwd); mq_close(fds[p].r_bwd);
            pin_to_cpu((2*p) % num_cpus());
            c2_initiator(fds[p].i_fwd, fds[p].i_bwd, iters, msz, pipes[p][1]);
            exit(0);
        }
    }

    /* Parent: close all MQ descriptors and all pipe write ends */
    for (int p = 0; p < n; p++) {
        mq_close(fds[p].i_fwd); mq_close(fds[p].r_bwd);
        mq_close(fds[p].r_fwd); mq_close(fds[p].i_bwd);
        close(pipes[p][1]);
    }
    free(fds);

    uint64_t t0 = now_ns();

    /* Drain all pipes sequentially. Safe because all initiator children are
     * already running concurrently — the pipe kernel buffer (>=64KB) absorbs
     * each child's output (iters*8 bytes) while the parent reads the others.
     * For iters > 8000 raise the pipe buffer before forking with:
     *   fcntl(pipes[p][1], F_SETPIPE_SZ, iters * sizeof(uint64_t))       */
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
    /* For C2 the meaningful metrics are latency percentiles.
     * elapsed covers pipe drain time, not the benchmark itself,
     * so throughput fields are left at zero.                    */
    result_t r = { .samples = flat, .n = total, .msg_size = msz,
                   .elapsed_ns = elapsed };
    compute_stats(&r);
    r.throughput_msg_s = 0;
    r.throughput_MB_s  = 0;
    print_csv_row(bench, n * 2, label, &r);

    for (int p = 0; p < n; p++) {
        snprintf(fn, sizeof(fn), C2_FWD, p);
        snprintf(bn, sizeof(bn), C2_BWD, p);
        mq_unlink(fn); mq_unlink(bn);
    }
    free(flat); free(pipes); free(resp_pids); free(init_pids);
    return 0;
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    char        mode[4] = "";
    int         n       = 4;
    uint32_t    k       = 5000;
    size_t      msz     = 64;
    const char *label   = "unspecified";
    int         opt;

    while ((opt = getopt(argc, argv, "m:n:k:s:p:H")) != -1) {
        switch (opt) {
        case 'm': strncpy(mode, optarg, 3);                      break;
        case 'n': n   = (int)strtol(optarg, NULL, 10);          break;
        case 'k': k   = (uint32_t)strtoul(optarg, NULL, 10);    break;
        case 's': msz = strtoul(optarg, NULL, 10);               break;
        case 'p': label = optarg;                                 break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: scalability -m <c1|c2> [-n N] [-k K] "
                            "[-s bytes] [-p label] [-H]\n");
            return 1;
        }
    }

    if      (strcmp(mode, "c1") == 0) return run_c1(n, k, msz, label);
    else if (strcmp(mode, "c2") == 0) return run_c2(n, (uint64_t)k, msz, label);
    else {
        fprintf(stderr, "error: -m is required, use c1 or c2\n");
        return 1;
    }
}
