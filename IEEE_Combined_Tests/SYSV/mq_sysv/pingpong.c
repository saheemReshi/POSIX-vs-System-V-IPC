/*
 * pingpong.c — Ping-Pong Latency via System V Message Queues
 *
 * Two processes, two queues (mirrors the POSIX version exactly):
 *   P1 --[q_fwd]--> P2
 *   P1 <--[q_bwd]-- P2
 *
 * P1 sends a message and blocks waiting for the reply.
 * P2 receives and immediately sends back.
 * RTT recorded each iteration; one-way latency = RTT / 2.
 * Throughput counts both directions: 2 * msg_size bytes per round trip.
 *
 * Both queue IDs are created before fork() so neither process races
 * to create its queue and the first sample is not inflated by
 * queue-creation time.
 *
 * System V differences vs POSIX:
 *   - Queues identified by integer msqid (from msgget), not named paths.
 *   - Messages wrapped in struct sv_msg { long mtype; char mtext[]; }.
 *   - mtype field used for routing: MTYPE_FWD on q_fwd, MTYPE_BWD on q_bwd.
 *   - No separate read/write descriptors — both ends share the same msqid.
 *   - Queue removed with msgctl(id, IPC_RMID, NULL) instead of mq_unlink.
 *   - Max message size raised with msgctl + struct msqid_ds if needed.
 *
 * Usage: pingpong [-n iters] [-s bytes] [-c cpu0,cpu1] [-p label] [-H]
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

/* Arbitrary IPC keys — change if they collide on your system */
#define KEY_FWD  0x1AB1F
#define KEY_BWD  0x1AB1B

/* mtype values — only one queue per msqid so any positive value works */
#define MTYPE_MSG  1L

/* System V message envelope */
typedef struct {
    long mtype;
    char mtext[1];   /* flexible; we over-allocate to msz bytes */
} sv_msg_t;

/* Helper: allocate an sv_msg_t with mtext large enough for msz bytes */
static sv_msg_t *alloc_msg(size_t msz)
{
    sv_msg_t *m = malloc(sizeof(long) + msz);
    if (!m) { perror("malloc msg"); exit(1); }
    m->mtype = MTYPE_MSG;
    return m;
}

/* ── P1: initiator ──────────────────────────────────────────────────── */

static void run_p1(int fwd, int bwd, uint64_t iters,
                   size_t msz, uint64_t *samples)
{
    sv_msg_t *msg = alloc_msg(msz);

    for (uint64_t i = 0; i < iters; i++) {
        memset(msg->mtext, (int)(i & 0xFF), msz);
        msg->mtype = MTYPE_MSG;

        uint64_t t0 = now_ns();

        if (msgsnd(fwd, msg, msz, 0) != 0)
            { perror("msgsnd p1 fwd"); exit(1); }
        if (msgrcv(bwd, msg, msz, MTYPE_MSG, 0) < 0)
            { perror("msgrcv p1 bwd"); exit(1); }

        samples[i] = now_ns() - t0;  /* RTT */
    }
    free(msg);
}

/* ── P2: responder ──────────────────────────────────────────────────── */

static void run_p2(int fwd, int bwd, uint64_t iters, size_t msz)
{
    sv_msg_t *msg = alloc_msg(msz);

    for (uint64_t i = 0; i < iters; i++) {
        if (msgrcv(fwd, msg, msz, MTYPE_MSG, 0) < 0)
            { perror("msgrcv p2 fwd"); exit(1); }
        msg->mtype = MTYPE_MSG;
        if (msgsnd(bwd, msg, msz, 0) != 0)
            { perror("msgsnd p2 bwd"); exit(1); }
    }
    free(msg);
}

/* ── helpers ────────────────────────────────────────────────────────── */

/*
 * Create a System V message queue with the given key.
 * Removes any stale queue with the same key first.
 * Raises the per-message size limit so large msz values work.
 */
static int create_queue(key_t key, size_t msz)
{
    /* Remove stale queue if present */
    int id = msgget(key, 0);
    if (id >= 0) msgctl(id, IPC_RMID, NULL);

    id = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
    if (id < 0) { perror("msgget"); return -1; }

    /*
     * Raise the per-message size limit via msgctl.
     * The kernel default (8192 bytes) is usually fine for small msz,
     * but we set it explicitly to match the requested size.
     * POSIX equivalent: struct mq_attr .mq_msgsize = msz.
     */
    struct msqid_ds ds;
    if (msgctl(id, IPC_STAT, &ds) != 0) { perror("msgctl IPC_STAT"); return -1; }
    ds.msg_qbytes = msz * 2;   /* depth=1 needs room for at least 1 message */
    if (msgctl(id, IPC_SET, &ds) != 0) {
        /* Non-fatal: default limit may already be sufficient */
        perror("msgctl IPC_SET msg_qbytes (non-fatal)");
    }

    return id;
}

static void remove_queue(int id)
{
    if (id >= 0) msgctl(id, IPC_RMID, NULL);
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    uint64_t    iters = 10000;
    size_t      msz   = 64;
    int         cpu0  = -1, cpu1 = -1;
    const char *label = "unspecified";
    int         run_id = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "n:s:c:p:r:H")) != -1) {
        switch (opt) {
        case 'n': iters = strtoull(optarg, NULL, 10);     break;
        case 's': msz   = strtoul(optarg, NULL, 10);      break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1); break;
        case 'p': label = optarg;                         break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10); break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: pingpong [-n iters] [-s bytes] "
                            "[-c cpu0,cpu1] [-p label] [-r run_id] [-H]\n");
            return 1;
        }
    }

    /*
     * Create both queues before fork() — same rationale as POSIX version:
     * no startup race, no queue-creation latency in the first sample.
     *
     * System V has no separate "open for read" / "open for write":
     * both processes simply use the same msqid returned by msgget.
     */
    int q_fwd = create_queue((key_t)KEY_FWD, msz);
    int q_bwd = create_queue((key_t)KEY_BWD, msz);
    if (q_fwd < 0 || q_bwd < 0) {
        fprintf(stderr, "try: sudo sysctl kernel.msgmax=%zu\n", msz);
        remove_queue(q_fwd); remove_queue(q_bwd);
        return 1;
    }

    uint64_t *samples = alloc_samples(iters);
    mem_snapshot_t mem_before = mem_snapshot();

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* Child = P2 (responder) */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_p2(q_fwd, q_bwd, iters, msz);
        exit(0);
    }

    /* Parent = P1 (initiator) */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    uint64_t t0      = now_ns();
    run_p1(q_fwd, q_bwd, iters, msz, samples);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);
    mem_snapshot_t mem_after = mem_snapshot();

    /* RTT → one-way latency */
    for (uint64_t i = 0; i < iters; i++) samples[i] /= 2;

    result_t r = { .samples = samples, .n = iters,
                   .msg_size = msz, .elapsed_ns = elapsed,
                   .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
                   .run_id = run_id };
    compute_stats(&r);

    /* Override throughput: each round trip moves msg_size in both directions */
    double sec = elapsed / 1e9;
    r.throughput_msg_s = iters / sec;
    r.throughput_MB_s  = (iters * 2 * msz) / sec / (1024.0 * 1024.0);

    print_csv_row("pingpong", 2, label, &r);

    remove_queue(q_fwd);
    remove_queue(q_bwd);
    free(samples);
    return 0;
}
