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

#include "../common/timing.h"
#include "../common/affinity.h"

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
    int         opt;

    while ((opt = getopt(argc, argv, "n:s:q:c:p:H")) != -1) {
        switch (opt) {
        case 'n': n_msgs = strtoull(optarg, NULL, 10); break;
        case 's': msz    = strtoul(optarg, NULL, 10);  break;
        case 'q': depth  = strtol(optarg, NULL, 10);   break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1); break;
        case 'p': label  = optarg;                      break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: throughput [-n msgs] [-s bytes] "
                            "[-q depth] [-c cpu0,cpu1] [-p label] [-H]\n");
            return 1;
        }
    }

    struct mq_attr attr = { .mq_maxmsg = depth, .mq_msgsize = (long)msz };
    mq_unlink(Q_NAME);

    /* Open both ends before fork so neither side races to open */
    mqd_t wq = mq_open(Q_NAME, O_CREAT | O_WRONLY, 0600, &attr);
    mqd_t rq = mq_open(Q_NAME, O_RDONLY);
    if (wq == (mqd_t)-1 || rq == (mqd_t)-1) {
        perror("mq_open");
        fprintf(stderr, "try: sudo sysctl fs.mqueue.msg_max=%ld "
                        "fs.mqueue.msgsize_max=%zu\n", depth, msz);
        return 1;
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

    uint64_t t0 = now_ns();
    run_producer(wq, n_msgs, msz);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);

    double sec = elapsed / 1e9;
    result_t r = {
        .n = n_msgs, .msg_size = msz, .elapsed_ns = elapsed,
        .avg_ns           = (double)elapsed / n_msgs,
        .throughput_msg_s = n_msgs / sec,
        .throughput_MB_s  = (n_msgs * msz) / sec / (1024.0 * 1024.0),
    };
    print_csv_row("throughput", 2, label, &r);

    mq_close(wq);
    mq_unlink(Q_NAME);
    return 0;
}
