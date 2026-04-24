/*
 * scalability.c — Scalability Under Contention via System V Message Queues
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
 * Both modes create all queues before fork() to avoid startup races.
 * Timing starts before any fork so no producer work is missed.
 *
 * System V differences vs POSIX:
 *   - Queues identified by integer msqid from msgget(key, IPC_CREAT|0600).
 *   - No named paths; keys derived from a base constant + index.
 *   - Messages wrapped in sv_msg_t { long mtype; char mtext[]; }.
 *   - Queue capacity set via msgctl IPC_SET on msg_qbytes field.
 *   - Cleanup: msgctl(id, IPC_RMID, NULL) replaces mq_unlink.
 *   - No separate read/write descriptors — all processes share msqid.
 *
 * Usage: scalability -m <c1|c2> [-n N] [-k msgs] [-s bytes] [-p label] [-H]
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

#define MTYPE_MSG  1L

/* Base IPC keys — index is added to distinguish queues */
#define KEY_C1_BASE   0x5C1000
#define KEY_C2_FWD    0x5C2F00
#define KEY_C2_BWD    0x5C2B00

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

/*
 * Create a System V message queue with the given key.
 * Removes any stale queue first.
 * Sets msg_qbytes = qbytes to give the queue enough capacity.
 */
static int create_queue(key_t key, size_t qbytes)
{
    int id = msgget(key, 0);
    if (id >= 0) msgctl(id, IPC_RMID, NULL);

    id = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
    if (id < 0) { perror("msgget"); return -1; }

    struct msqid_ds ds;
    if (msgctl(id, IPC_STAT, &ds) == 0) {
        ds.msg_qbytes = qbytes;
        if (msgctl(id, IPC_SET, &ds) != 0)
            perror("msgctl IPC_SET (non-fatal)");
    }
    return id;
}

static void remove_queue(int id)
{
    if (id >= 0) msgctl(id, IPC_RMID, NULL);
}

/* ════════════════════════════════════════════════════════════════════
 * C1: Fan-in — N producers → 1 consumer through one shared queue
 * ════════════════════════════════════════════════════════════════════ */

static void c1_producer(int msqid, uint32_t k, size_t msz, int id)
{
    sv_msg_t *msg = alloc_msg(msz);
    memset(msg->mtext, id & 0xFF, msz);

    for (uint32_t i = 0; i < k; i++) {
        msg->mtext[0] = (char)(i & 0xFF);
        msg->mtype    = MTYPE_MSG;
        if (msgsnd(msqid, msg, msz, 0) != 0)
            { perror("msgsnd c1 producer"); exit(1); }
    }
    free(msg);
}

static void c1_consumer(int msqid, uint64_t total, size_t msz)
{
    sv_msg_t *msg = alloc_msg(msz);

    for (uint64_t i = 0; i < total; i++)
        if (msgrcv(msqid, msg, msz, MTYPE_MSG, 0) < 0)
            { perror("msgrcv c1 consumer"); exit(1); }

    free(msg);
}

static int run_c1(int n, uint32_t k, size_t msz, const char *label, int run_id)
{
    /*
     * Queue capacity = n*k messages, each (sizeof(long) + msz) bytes.
     * Capped so we don't exceed the kernel's msgmnb limit.
     * POSIX equivalent: .mq_maxmsg = min(n*k, 1024).
     *
     * If the queue fills up, msgsnd blocks — producers slow down but
     * correctness is preserved, which is identical to the POSIX version.
     */
    long      depth  = (long)n * k;
    if (depth > 1024) depth = 1024;
    size_t    qbytes = (size_t)depth * (sizeof(long) + msz);

    int msqid = create_queue((key_t)KEY_C1_BASE, qbytes);
    if (msqid < 0) {
        fprintf(stderr, "try: sudo sysctl kernel.msgmnb=%zu "
                        "kernel.msgmax=%zu\n", qbytes, msz);
        return 1;
    }

    /* Start timing before forks so we capture all producer work */
    uint64_t t0 = now_ns();

    pid_t *pids = malloc(n * sizeof(pid_t));
    if (!pids) { perror("malloc"); remove_queue(msqid); return 1; }

    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork c1"); free(pids); remove_queue(msqid); return 1; }
        if (pids[i] == 0) {
            /*
             * System V: no per-process descriptor to close.
             * All children simply use the shared msqid.
             */
            pin_to_cpu(i % num_cpus());
            c1_producer(msqid, k, msz, i);
            exit(0);
        }
    }

    /* Parent is the consumer */
    pin_to_cpu(n % num_cpus());
    mem_snapshot_t mem_before = mem_snapshot();
    c1_consumer(msqid, (uint64_t)n * k, msz);
    uint64_t elapsed = now_ns() - t0;
    mem_snapshot_t mem_after = mem_snapshot();

    for (int i = 0; i < n; i++) waitpid(pids[i], NULL, 0);
    free(pids);

    uint64_t total = (uint64_t)n * k;
    double   sec   = elapsed / 1e9;
    char     bench[32];
    snprintf(bench, sizeof(bench), "fanin_n%d", n);

    result_t r = {
        .n                = total,
        .msg_size         = msz,
        .elapsed_ns       = elapsed,
        .avg_ns           = (double)elapsed / total,
        .throughput_msg_s = total / sec,
        .throughput_MB_s  = (total * msz) / sec / (1024.0 * 1024.0),
        .mem_delta_kb     = mem_delta_kb(&mem_before, &mem_after),
        .run_id           = run_id,
    };
    print_csv_row(bench, n + 1, label, &r);

    remove_queue(msqid);
    return 0;
}

/* ════════════════════════════════════════════════════════════════════
 * C2: Parallel pairs — N independent ping-pong pairs
 * ════════════════════════════════════════════════════════════════════ */

/* Per-pair queue IDs, created before fork */
typedef struct { int fwd; int bwd; } pair_ids_t;

static void c2_responder(int fwd, int bwd, uint64_t iters, size_t msz)
{
    sv_msg_t *msg = alloc_msg(msz);

    for (uint64_t i = 0; i < iters; i++) {
        if (msgrcv(fwd, msg, msz, MTYPE_MSG, 0) < 0)
            { perror("msgrcv c2 responder"); exit(1); }
        msg->mtype = MTYPE_MSG;
        if (msgsnd(bwd, msg, msz, 0) != 0)
            { perror("msgsnd c2 responder"); exit(1); }
    }
    free(msg);
}

static void c2_initiator(int fwd, int bwd, uint64_t iters,
                          size_t msz, int pipe_wr)
{
    sv_msg_t *msg = alloc_msg(msz);
    uint64_t *smp = alloc_samples(iters);

    for (uint64_t i = 0; i < iters; i++) {
        memset(msg->mtext, (int)(i & 0xFF), msz);
        msg->mtype = MTYPE_MSG;

        uint64_t t0 = now_ns();
        if (msgsnd(fwd, msg, msz, 0) != 0)
            { perror("msgsnd c2 initiator"); exit(1); }
        if (msgrcv(bwd, msg, msz, MTYPE_MSG, 0) < 0)
            { perror("msgrcv c2 initiator"); exit(1); }
        smp[i] = (now_ns() - t0) / 2;   /* one-way latency */
    }
    free(msg);

    /* Send samples to parent via pipe — same mechanism as POSIX version */
    size_t      remaining = iters * sizeof(uint64_t);
    const char *ptr       = (const char *)smp;
    while (remaining > 0) {
        ssize_t w = write(pipe_wr, ptr, remaining);
        if (w <= 0) { perror("pipe write c2"); exit(1); }
        ptr += w; remaining -= (size_t)w;
    }
    free(smp);
    close(pipe_wr);
}

static int run_c2(int n, uint64_t iters, size_t msz, const char *label, int run_id)
{
    /*
     * depth=1 per pair: every send blocks until the peer receives,
     * so msg_qbytes needs room for exactly one message.
     */
    size_t qbytes = sizeof(long) + msz;

    pair_ids_t *ids = malloc(n * sizeof(pair_ids_t));
    if (!ids) { perror("malloc ids"); return 1; }

    /* Create all pairs' queues before any fork() */
    for (int p = 0; p < n; p++) {
        ids[p].fwd = create_queue((key_t)(KEY_C2_FWD + p), qbytes);
        ids[p].bwd = create_queue((key_t)(KEY_C2_BWD + p), qbytes);
        if (ids[p].fwd < 0 || ids[p].bwd < 0) {
            fprintf(stderr, "try: sudo sysctl kernel.msgmax=%zu\n", msz);
            /* cleanup already-created queues */
            for (int q = 0; q < p; q++) {
                remove_queue(ids[q].fwd);
                remove_queue(ids[q].bwd);
            }
            free(ids); return 1;
        }
    }

    /* One pipe per pair for returning samples to parent */
    int      (*pipes)[2]  = malloc(n * sizeof(*pipes));
    pid_t    *resp_pids   = malloc(n * sizeof(pid_t));
    pid_t    *init_pids   = malloc(n * sizeof(pid_t));
    if (!pipes || !resp_pids || !init_pids) { perror("malloc"); return 1; }

    for (int p = 0; p < n; p++)
        if (pipe(pipes[p]) != 0) { perror("pipe"); return 1; }

    /* Fork all children — after this point all 2*N processes run concurrently */
    for (int p = 0; p < n; p++) {

        resp_pids[p] = fork();
        if (resp_pids[p] < 0) { perror("fork responder"); return 1; }
        if (resp_pids[p] == 0) {
            /*
             * System V: no descriptors to close per-pair.
             * Close all pipe ends — this process does not use pipes.
             */
            for (int q = 0; q < n; q++) {
                close(pipes[q][0]);
                close(pipes[q][1]);
            }
            pin_to_cpu((2*p + 1) % num_cpus());
            c2_responder(ids[p].fwd, ids[p].bwd, iters, msz);
            exit(0);
        }

        init_pids[p] = fork();
        if (init_pids[p] < 0) { perror("fork initiator"); return 1; }
        if (init_pids[p] == 0) {
            /* Close all pipes except this pair's write end */
            for (int q = 0; q < n; q++) {
                close(pipes[q][0]);
                if (q != p) close(pipes[q][1]);
            }
            pin_to_cpu((2*p) % num_cpus());
            c2_initiator(ids[p].fwd, ids[p].bwd, iters, msz, pipes[p][1]);
            exit(0);
        }
    }

    /* Parent: close all pipe write ends */
    for (int p = 0; p < n; p++) close(pipes[p][1]);
    free(ids);   /* System V: no MQ descriptors to close in parent */

    mem_snapshot_t mem_before = mem_snapshot();
    uint64_t t0 = now_ns();

    /* Drain all pipes — all initiator children run concurrently */
    uint64_t  total = (uint64_t)n * iters;
    uint64_t *flat  = malloc(total * sizeof(uint64_t));
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
    mem_snapshot_t mem_after = mem_snapshot();

    char bench[32];
    snprintf(bench, sizeof(bench), "pairs_n%d", n);

    result_t r = { .samples = flat, .n = total, .msg_size = msz,
                   .elapsed_ns = elapsed,
                   .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
                   .run_id = run_id };
    compute_stats(&r);
    r.throughput_msg_s = 0;   /* latency benchmark — throughput not meaningful */
    r.throughput_MB_s  = 0;
    print_csv_row(bench, n * 2, label, &r);

    /* Cleanup: remove all pair queues */
    for (int p = 0; p < n; p++) {
        remove_queue(msgget((key_t)(KEY_C2_FWD + p), 0));
        remove_queue(msgget((key_t)(KEY_C2_BWD + p), 0));
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
    int         run_id  = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "m:n:k:s:p:r:H")) != -1) {
        switch (opt) {
        case 'm': strncpy(mode, optarg, 3);                   break;
        case 'n': n   = (int)strtol(optarg, NULL, 10);       break;
        case 'k': k   = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 's': msz = strtoul(optarg, NULL, 10);            break;
        case 'p': label = optarg;                              break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10);     break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: scalability -m <c1|c2> [-n N] [-k K] "
                            "[-s bytes] [-p label] [-r run_id] [-H]\n");
            return 1;
        }
    }

    if      (strcmp(mode, "c1") == 0) return run_c1(n, k, msz, label, run_id);
    else if (strcmp(mode, "c2") == 0) return run_c2(n, (uint64_t)k, msz, label, run_id);
    else {
        fprintf(stderr, "error: -m is required, use c1 or c2\n");
        return 1;
    }
}
