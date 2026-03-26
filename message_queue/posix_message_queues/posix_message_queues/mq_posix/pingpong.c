/*
 * pingpong.c — Ping-Pong Latency via POSIX Message Queues
 *
 * Two processes, two queues:
 *   P1 --[q_fwd]--> P2
 *   P1 <--[q_bwd]-- P2
 *
 * P1 sends a message and blocks waiting for the reply.
 * P2 receives and immediately sends back.
 * RTT recorded each iteration; one-way latency = RTT / 2.
 * Throughput counts both directions: 2 * msg_size bytes per round trip.
 *
 * All four queue descriptors are opened before fork() so neither
 * process races to open its end and the first sample is not inflated
 * by queue-open time.
 *
 * Usage: pingpong [-n iters] [-s bytes] [-c cpu0,cpu1] [-p label] [-H]
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

#define Q_FWD "/ipc_pp_fwd"
#define Q_BWD "/ipc_pp_bwd"

static void run_p1(mqd_t fwd, mqd_t bwd, uint64_t iters,
                   size_t msz, uint64_t *samples)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }

    for (uint64_t i = 0; i < iters; i++) {
        memset(buf, (int)(i & 0xFF), msz);
        uint64_t t0 = now_ns();
        if (mq_send(fwd, buf, msz, 0) != 0)     { perror("mq_send");    exit(1); }
        if (mq_receive(bwd, buf, msz, NULL) < 0) { perror("mq_receive"); exit(1); }
        samples[i] = now_ns() - t0;  /* RTT */
    }
    free(buf);
}

static void run_p2(mqd_t fwd, mqd_t bwd, uint64_t iters, size_t msz)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }

    for (uint64_t i = 0; i < iters; i++) {
        if (mq_receive(fwd, buf, msz, NULL) < 0) { perror("mq_receive"); exit(1); }
        if (mq_send(bwd, buf, msz, 0) != 0)      { perror("mq_send");    exit(1); }
    }
    free(buf);
}

int main(int argc, char *argv[])
{
    uint64_t    iters = 10000;
    size_t      msz   = 64;
    int         cpu0  = -1, cpu1 = -1;
    const char *label = "unspecified";
    int         opt;

    while ((opt = getopt(argc, argv, "n:s:c:p:H")) != -1) {
        switch (opt) {
        case 'n': iters = strtoull(optarg, NULL, 10);      break;
        case 's': msz   = strtoul(optarg, NULL, 10);       break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1);  break;
        case 'p': label = optarg;                          break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: pingpong [-n iters] [-s bytes] "
                            "[-c cpu0,cpu1] [-p label] [-H]\n");
            return 1;
        }
    }

    /* depth=1: every mq_send blocks until the peer calls mq_receive,
     * making each iteration a genuine kernel round-trip.               */
    struct mq_attr attr = { .mq_maxmsg = 1, .mq_msgsize = (long)msz };
    mq_unlink(Q_FWD); mq_unlink(Q_BWD);

    /* Open all four descriptors before fork() — no startup race,
     * no queue-open latency leaking into the first sample.            */
    mqd_t p1_fwd = mq_open(Q_FWD, O_CREAT | O_WRONLY, 0600, &attr);
    mqd_t p2_bwd = mq_open(Q_BWD, O_CREAT | O_WRONLY, 0600, &attr);
    mqd_t p2_fwd = mq_open(Q_FWD, O_RDONLY);
    mqd_t p1_bwd = mq_open(Q_BWD, O_RDONLY);
    if (p1_fwd==(mqd_t)-1 || p2_bwd==(mqd_t)-1 ||
        p2_fwd==(mqd_t)-1 || p1_bwd==(mqd_t)-1) {
        perror("mq_open");
        fprintf(stderr, "try: sudo sysctl fs.mqueue.msgsize_max=%zu\n", msz);
        return 1;
    }

    uint64_t *samples = alloc_samples(iters);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        mq_close(p1_fwd); mq_close(p1_bwd);
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_p2(p2_fwd, p2_bwd, iters, msz);
        mq_close(p2_fwd); mq_close(p2_bwd);
        exit(0);
    }

    mq_close(p2_fwd); mq_close(p2_bwd);
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    uint64_t t0 = now_ns();
    run_p1(p1_fwd, p1_bwd, iters, msz, samples);
    uint64_t elapsed = now_ns() - t0;
    waitpid(pid, NULL, 0);

    /* RTT → one-way latency */
    for (uint64_t i = 0; i < iters; i++) samples[i] /= 2;

    result_t r = { .samples = samples, .n = iters,
                   .msg_size = msz, .elapsed_ns = elapsed };
    compute_stats(&r);
    /* Override throughput: each round trip moves msg_size bytes in both
     * directions, so effective bandwidth is 2x the one-way payload.
     * compute_stats uses msg_size*1 so we correct it here after the call. */
    double sec = elapsed / 1e9;
    r.throughput_msg_s = iters / sec;
    r.throughput_MB_s  = (iters * 2 * msz) / sec / (1024.0 * 1024.0);
    print_csv_row("pingpong", 2, label, &r);

    mq_close(p1_fwd); mq_close(p1_bwd);
    mq_unlink(Q_FWD); mq_unlink(Q_BWD);
    free(samples);
    return 0;
}
