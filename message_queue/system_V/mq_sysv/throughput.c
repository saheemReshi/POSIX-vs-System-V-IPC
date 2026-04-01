/*
 * throughput.c — Producer→Consumer Throughput via System V Message Queues
 *
 * One producer sends N messages; one consumer receives them.
 * Queue depth > 1 lets the producer run ahead without blocking,
 * measuring peak kernel MQ bandwidth.
 *
 * Both processes share the same msqid (System V has no separate
 * read/write descriptors). The queue is created before fork() to
 * avoid startup races — identical rationale to the POSIX version.
 *
 * System V differences vs POSIX:
 *   - msgget(key, IPC_CREAT|0600) replaces mq_open(name, O_CREAT|...).
 *   - msgsnd / msgrcv replace mq_send / mq_receive.
 *   - Queue capacity is msg_qbytes (total bytes), not msg_maxmsg (count).
 *     We set msg_qbytes = depth * (sizeof(long) + msz) via msgctl IPC_SET.
 *   - Cleanup: msgctl(id, IPC_RMID, NULL) replaces mq_unlink.
 *
 * Usage: throughput [-n msgs] [-s bytes] [-q depth] [-c cpu0,cpu1]
 *                   [-p label] [-H]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>

#include "../common/timing.h"
#include "../common/affinity.h"

#define KEY_THROUGHPUT  0x1AB20

#define MTYPE_MSG  1L

typedef struct {
    long mtype;
    char mtext[1];
} sv_msg_t;

static sv_msg_t *alloc_msg(size_t msz)
{
    sv_msg_t *m = malloc(sizeof(long) + msz);
    if (!m) { perror("malloc msg"); exit(1); }
    m->mtype = MTYPE_MSG;
    return m;
}

/* ── producer ───────────────────────────────────────────────────────── */

static void run_producer(int msqid, uint64_t n, size_t msz)
{
    sv_msg_t *msg = alloc_msg(msz);
    memset(msg->mtext, 0xAB, msz);

    for (uint64_t i = 0; i < n; i++) {
        msg->mtext[0] = (char)(i & 0xFF);
        msg->mtype    = MTYPE_MSG;
        if (msgsnd(msqid, msg, msz, 0) != 0)
            { perror("msgsnd producer"); exit(1); }
    }
    free(msg);
}

/* ── consumer ───────────────────────────────────────────────────────── */

static void run_consumer(int msqid, uint64_t n, size_t msz)
{
    sv_msg_t *msg = alloc_msg(msz);
    volatile char sink = 0;

    for (uint64_t i = 0; i < n; i++) {
        if (msgrcv(msqid, msg, msz, MTYPE_MSG, 0) < 0)
            { perror("msgrcv consumer"); exit(1); }
        sink += msg->mtext[0];
    }
    (void)sink;
    free(msg);
    /*
     * volatile sink prevents the compiler from eliminating the buffer
     * read — same rationale as in the POSIX version.
     */
}

/* ── main ───────────────────────────────────────────────────────────── */

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
        case 'n': n_msgs = strtoull(optarg, NULL, 10);      break;
        case 's': msz    = strtoul(optarg, NULL, 10);       break;
        case 'q': depth  = strtol(optarg, NULL, 10);        break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1);   break;
        case 'p': label  = optarg;                          break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: throughput [-n msgs] [-s bytes] "
                            "[-q depth] [-c cpu0,cpu1] [-p label] [-H]\n");
            return 1;
        }
    }

    /* Remove any stale queue with the same key */
    int stale = msgget((key_t)KEY_THROUGHPUT, 0);
    if (stale >= 0) msgctl(stale, IPC_RMID, NULL);

    int msqid = msgget((key_t)KEY_THROUGHPUT, IPC_CREAT | IPC_EXCL | 0600);
    if (msqid < 0) {
        perror("msgget");
        fprintf(stderr, "try: sudo sysctl kernel.msgmax=%zu "
                        "kernel.msgmnb=%ld\n",
                msz, depth * (long)(sizeof(long) + msz));
        return 1;
    }

    /*
     * Set queue capacity = depth * (mtype + mtext).
     * POSIX equivalent: struct mq_attr { .mq_maxmsg = depth, .mq_msgsize = msz }.
     * System V counts bytes (msg_qbytes), not messages:
     *   each message occupies sizeof(long) [mtype] + msz [mtext] bytes.
     */
    struct msqid_ds ds;
    msgctl(msqid, IPC_STAT, &ds);
    ds.msg_qbytes = (unsigned long)depth * (sizeof(long) + msz);
    if (msgctl(msqid, IPC_SET, &ds) != 0)
        perror("msgctl IPC_SET msg_qbytes (non-fatal)");

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); msgctl(msqid, IPC_RMID, NULL); return 1; }

    if (pid == 0) {
        /* Child = consumer */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_consumer(msqid, n_msgs, msz);
        exit(0);
    }

    /* Parent = producer */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    uint64_t t0      = now_ns();
    run_producer(msqid, n_msgs, msz);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);

    double sec = elapsed / 1e9;
    result_t r = {
        .n                = n_msgs,
        .msg_size         = msz,
        .elapsed_ns       = elapsed,
        .avg_ns           = (double)elapsed / n_msgs,
        .throughput_msg_s = n_msgs / sec,
        .throughput_MB_s  = (n_msgs * msz) / sec / (1024.0 * 1024.0),
    };
    print_csv_row("throughput", 2, label, &r);

    msgctl(msqid, IPC_RMID, NULL);
    return 0;
}
