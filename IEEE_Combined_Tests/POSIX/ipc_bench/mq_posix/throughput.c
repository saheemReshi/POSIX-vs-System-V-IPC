/*
 * throughput.c — Producer→Consumer Throughput via POSIX Message Queues
 *
 * One producer sends N messages; one consumer receives them.
 * Queue depth > 1 lets the producer run ahead without blocking,
 * measuring peak kernel MQ bandwidth.
 *
 * Both ends open their descriptors before fork() to avoid startup races.
 * Elapsed time is measured around the producer loop only (producer-side
 * bandwidth), which is the standard definition for this benchmark.
 *
 * Usage: throughput [-n msgs] [-s bytes] [-q depth] [-c cpu0,cpu1]
 *                   [-p label] [-H]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

#define Q_NAME "/ipc_throughput"

static void run_producer(mqd_t q, uint64_t n, size_t msz)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    memset(buf, 0xAB, msz);

    for (uint64_t i = 0; i < n; i++) {
        buf[0] = (char)(i & 0xFF);
        if (mq_send(q, buf, msz, 0) != 0) { perror("mq_send"); exit(1); }
    }
    free(buf);
}

static void run_consumer(mqd_t q, uint64_t n, size_t msz)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    volatile char sink = 0;

    for (uint64_t i = 0; i < n; i++) {
        if (mq_receive(q, buf, msz, NULL) < 0) { perror("mq_receive"); exit(1); }
        sink += buf[0];
    }
    (void)sink;
    free(buf);
    /*In performance measurements you must ensure the compiler cannot eliminate work.
    Without the sink trick the compiler might conclude:  (using volatile, so compiler doesn't use aggressive optimisations)
    buf contents are never used
    and remove the buffer read.
    Then you would measure:
    mq_receive() + minimal code
    instead of
    mq_receive() + real memory access
    which makes the benchmark unrealistic.*/
}

int main(int argc, char *argv[])
{
    uint64_t    n_msgs = 100000;
    size_t      msz    = 64;
    long        depth  = 64;
    int         cpu0   = -1, cpu1 = -1;
    const char *label  = "unspecified";
    int         run_id = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "n:s:q:c:p:r:H")) != -1) {
        switch (opt) {
        case 'n': n_msgs = strtoull(optarg, NULL, 10); break;
        case 's': msz    = strtoul(optarg, NULL, 10);  break;
        case 'q': depth  = strtol(optarg, NULL, 10);   break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1); break;
        case 'p': label  = optarg;                      break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10); break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: throughput [-n msgs] [-s bytes] "
                            "[-q depth] [-c cpu0,cpu1] [-p label] "
                            "[-r run_id] [-H]\n");
            return 1;
        }
    }

    mq_unlink(Q_NAME);

    /*
     * Auto-degrade queue depth when the kernel rejects mq_open().
     *
     * Linux can return several different errnos depending on kernel version
     * when depth * (msg_size + overhead) > RLIMIT_MSGQUEUE:
     *   EINVAL  — most common (resource limit exceeded)
     *   EMSGSIZE — msg_size > msgsize_max
     *   ENOENT  — seen on some kernels when partial creation fails
     *   ENOMEM  — kernel cannot allocate queue memory
     *   EMFILE  — per-process queue limit reached
     *   ENFILE  — system-wide queue limit reached
     *
     * Strategy: for any failure when depth > 1, halve depth and retry.
     * Only give up permanently on errors unrelated to sizing:
     *   EACCES (permissions) or EEXIST (name collision — shouldn't happen
     *   after mq_unlink, but we guard it anyway).
     * At depth == 1 any failure is a hard error.
     */
    mqd_t wq = (mqd_t)-1;
    long actual_depth = depth;
    while (actual_depth >= 1) {
        struct mq_attr attr = { .mq_maxmsg = actual_depth,
                                .mq_msgsize = (long)msz };
        mq_unlink(Q_NAME);   /* clear any half-created entry from prior attempt */
        wq = mq_open(Q_NAME, O_CREAT | O_WRONLY, 0600, &attr);
        if (wq != (mqd_t)-1) break;

        /* Hard errors — not a sizing problem, fail immediately */
        if (errno == EACCES || errno == EEXIST) {
            perror("mq_open"); return 1;
        }
        /* At minimum depth, any error is fatal */
        if (actual_depth == 1) {
            perror("mq_open depth=1");
            fprintf(stderr,
                "throughput: cannot open queue at depth=1 for msg_size=%zu.\n"
                "  Check: sudo sysctl fs.mqueue.msgsize_max=%zu\n",
                msz, msz);
            return 1;
        }
        /* All other errors: treat as resource-limit, halve and retry */
        long prev = actual_depth;
        actual_depth /= 2;
        /* Suppress per-attempt noise — only the final summary is printed */
        (void)prev;
    }
    if (actual_depth != depth) {
        fprintf(stderr,
            "throughput: depth capped at %ld (requested %ld) for msg_size=%zu "
            "due to RLIMIT_MSGQUEUE.\n"
            "  To fix: echo '* hard msgqueue unlimited' | "
            "sudo tee -a /etc/security/limits.conf\n",
            actual_depth, depth, msz);
    }

    mqd_t rq = mq_open(Q_NAME, O_RDONLY);
    if (rq == (mqd_t)-1) {
        perror("mq_open rq"); mq_close(wq); mq_unlink(Q_NAME); return 1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        mq_close(wq);   /* consumer only needs read end */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_consumer(rq, n_msgs, msz);
        mq_close(rq);
        exit(0);
    }

    mq_close(rq);   /* producer only needs write end */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    mem_snapshot_t mem_before = mem_snapshot();
    uint64_t t0 = now_ns();
    run_producer(wq, n_msgs, msz);
    uint64_t elapsed = now_ns() - t0;
    mem_snapshot_t mem_after = mem_snapshot();

    waitpid(pid, NULL, 0);

    double sec = elapsed / 1e9;
    result_t r = {
        .n = n_msgs, .msg_size = msz, .elapsed_ns = elapsed,
        .avg_ns           = (double)elapsed / n_msgs,
        .throughput_msg_s = n_msgs / sec,
        .throughput_MB_s  = (n_msgs * msz) / sec / (1024.0 * 1024.0),
        .mem_delta_kb     = mem_delta_kb(&mem_before, &mem_after),
        .run_id           = run_id,
    };
    print_csv_row("throughput", 2, label, &r);

    mq_close(wq);
    mq_unlink(Q_NAME);
    return 0;
}
