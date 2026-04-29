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
#include "../common/stats.h"

#define KEY_THROUGHPUT  0x1AB20

#define MTYPE_MSG  1L

#define WARMUP_FRAC 0.05
#define WARMUP_MAX  5000

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
    int         run_id = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "n:s:q:c:p:r:H")) != -1) {
        switch (opt) {
        case 'n': n_msgs = strtoull(optarg, NULL, 10);      break;
        case 's': msz    = strtoul(optarg, NULL, 10);       break;
        case 'q': depth  = strtol(optarg, NULL, 10);        break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1);   break;
        case 'p': label  = optarg;                          break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10);   break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: throughput [-n msgs] [-s bytes] "
                            "[-q depth] [-c cpu0,cpu1] [-p label] [-r run_id] [-H]\n");
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
     *
     * IPC_SET fails (silently truncating capacity) when msgmnb < requested.
     * That would make the depth parameter meaningless beyond the cap, so we
     * make it a hard error and ask the user to raise kernel.msgmnb.
     */
    struct msqid_ds ds;
    msgctl(msqid, IPC_STAT, &ds);
    unsigned long requested_qbytes = (unsigned long)depth * (sizeof(long) + msz);
    ds.msg_qbytes = requested_qbytes;
    if (msgctl(msqid, IPC_SET, &ds) != 0) {
        fprintf(stderr,
            "throughput: msgctl(IPC_SET) failed for msg_qbytes=%lu (errno=%d).\n"
            "  Required for depth=%ld, msg_size=%zu.\n"
            "  Raise the kernel limit, e.g.:\n"
            "    sudo sysctl -w kernel.msgmnb=%lu\n",
            requested_qbytes, errno, depth, msz, requested_qbytes);
        msgctl(msqid, IPC_RMID, NULL);
        return 1;
    }
    /* Verify the kernel actually applied the value (some kernels round) */
    if (msgctl(msqid, IPC_STAT, &ds) == 0 && ds.msg_qbytes < requested_qbytes) {
        fprintf(stderr,
            "throughput: kernel applied msg_qbytes=%lu (< requested %lu).\n"
            "  Raise: sudo sysctl -w kernel.msgmnb=%lu\n",
            (unsigned long)ds.msg_qbytes, requested_qbytes, requested_qbytes);
        msgctl(msqid, IPC_RMID, NULL);
        return 1;
    }

    uint64_t warmup = (uint64_t)((double)n_msgs * WARMUP_FRAC);
    if (warmup > WARMUP_MAX) warmup = WARMUP_MAX;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); msgctl(msqid, IPC_RMID, NULL); return 1; }

    if (pid == 0) {
        /* Child = consumer */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        if (warmup) run_consumer(msqid, warmup, msz);
        run_consumer(msqid, n_msgs, msz);
        exit(0);
    }

    /* Parent = producer */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    if (warmup) run_producer(msqid, warmup, msz);

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
        .mem_delta_kb     = 0,
        .run_id           = run_id,
    };
    print_csv_row_mech("mq_sysv", "throughput", 2, label, &r);

    msgctl(msqid, IPC_RMID, NULL);
    return 0;
}
