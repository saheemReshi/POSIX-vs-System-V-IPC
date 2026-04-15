/*
 * priority_test.c — POSIX MQ Message Priority Benchmarks
 *
 * Three sub-tests measuring DIFFERENT aspects of priority scheduling.
 * All three use CONCURRENT send + receive so the kernel's priority queue
 * is exercised under real load, not in a pre-filled batch.
 *
 * ── T1: Head-of-line jump measurement ────────────────────────────────────
 *
 *   Producer sends a CONTINUOUS stream of alternating LOW/HIGH messages
 *   into a queue of depth D.  Consumer drains concurrently.  We measure
 *   the "priority jump distance": how many positions ahead of its FIFO
 *   arrival order each HIGH message is actually delivered.
 *
 *   Queue depth D > 1 is essential.  With D=1 there is never more than one
 *   message queued, so the kernel never has a choice between priorities —
 *   it always delivers the only message present.  With D=8 the producer
 *   can run ahead and queue a mix of LOW and HIGH; the kernel then picks
 *   HIGH first regardless of arrival order.  That's what we measure.
 *
 *   Key metric: jump_distance = FIFO_position - actual_receive_position
 *   A HIGH message with jump_distance=3 was delivered 3 positions earlier
 *   than FIFO ordering would have given it.
 *
 *   Also measures: one-way latency per priority (embed send timestamp in
 *   message, compare to receive timestamp in child consumer via pipe).
 *
 * ── T2: Backpressure latency divergence ──────────────────────────────────
 *
 *   Measures latency divergence between HIGH and LOW priority messages
 *   when the queue is under backpressure.  Consumer deliberately throttles
 *   at 1 µs between receives so the queue stays partially full.  Producer
 *   sends as fast as possible.  Now the kernel always has a choice and
 *   HIGH messages consistently skip ahead of LOW ones.
 *
 *   With no backpressure (depth=1, fast consumer) the average latency of
 *   LOW and HIGH is nearly identical — as seen in the smoke test.  This
 *   test isolates the backpressure condition that makes priority matter.
 *
 *   Reports: per-priority p50/p95/p99/p99.9 latency distributions.
 *   The divergence between HIGH and LOW at p99 is the publishable result.
 *
 * ── T3: PPR vs N-producer sweep with start barrier ───────────────────────
 *
 *   N producers all start simultaneously (synchronised via a pipe barrier)
 *   and send concurrently while the consumer drains.  This creates genuine
 *   contention: the kernel's priority queue must correctly order messages
 *   from multiple concurrent writers.
 *
 *   Priority Preservation Ratio (PPR) = messages received in non-increasing
 *   priority order / total messages.
 *
 *   The start barrier is critical.  Without it, producers fill the queue
 *   sequentially before the consumer starts — the "priority" test degrades
 *   into a simple ordering test on a static array, which trivially passes.
 *   With the barrier, all producers and the consumer run simultaneously,
 *   creating the scheduling races that make PPR interesting.
 *
 *   Sweep N = 1, 2, 4, 8.  Plot PPR vs N.  Any degradation below 1.0
 *   reveals real priority inversion under concurrent multi-producer load.
 *   This is the comparison point against SysV, which has no priority
 *   mechanism at all (mtype is a filter, not a priority).
 *
 * ── Output ────────────────────────────────────────────────────────────────
 *
 *   CSV rows: mechanism=mq_posix, benchmark=priority_t{1|2_low|2_high|3}
 *   stderr:   human-readable summary lines prefixed with [T1]/[T2]/[T3]
 *   stdout:   # comment lines for PPR and jump statistics (parsed by
 *             analysis/analyze.py for supplemental tables)
 *
 * Usage:
 *   priority_test -t <1|2|3> [-n msgs] [-s bytes] [-d depth]
 *                 [-N procs] [-c cpu0,cpu1] [-p label] [-r run_id] [-H]
 *
 *   -t  sub-test (required)
 *   -n  messages per priority level (default 5000)
 *   -s  message size in bytes (default 64, minimum 16 for embedded header)
 *   -d  queue depth for T1/T2 (default 8; must be > 1 to see priority effect)
 *   -N  number of producers for T3 (default 4)
 *   -c  cpu0,cpu1 — pin producer and consumer to specific CPUs
 *   -p  placement label for CSV
 *   -r  run_id (for 30-run orchestration)
 *   -H  print CSV header and exit
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/wait.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

/* Priority levels — gap of 29 ensures kernel has unambiguous ordering */
#define PRIO_LOW   1
#define PRIO_HIGH  30

#define Q_BASE "/ipc_prio"

/* ── Embedded message header (16 bytes, fits in any msg_size >= 16) ──────── */
typedef struct {
    uint64_t send_ns;    /* timestamp at mq_send() — for one-way latency   */
    uint32_t seqno;      /* monotonic sequence number from producer         */
    uint8_t  priority;   /* sender's priority (PRIO_LOW or PRIO_HIGH)       */
    uint8_t  pad[3];
} msg_hdr_t;

static void fill_msg(char *buf, size_t msz, uint64_t ts,
                     uint32_t seq, uint8_t prio)
{
    msg_hdr_t h = { .send_ns = ts, .seqno = seq, .priority = prio };
    memcpy(buf, &h, sizeof(h));
    if (msz > sizeof(h))
        memset(buf + sizeof(h), (int)prio, msz - sizeof(h));
}

/* ── Sample returned from child consumer to parent via pipe ─────────────── */
typedef struct {
    uint64_t latency_ns;
    uint32_t seqno;          /* producer's FIFO sequence number    */
    uint32_t recv_position;  /* order in which consumer received   */
    uint8_t  priority;
    uint8_t  pad[3];
} recv_sample_t;

/* ══════════════════════════════════════════════════════════════════════════
 * T1 — Head-of-line jump measurement under concurrent load
 * ══════════════════════════════════════════════════════════════════════════ */

static int run_t1(uint32_t n, size_t msz, long depth,
                  int cpu0, int cpu1, const char *label, int run_id)
{
    if (msz < sizeof(msg_hdr_t)) msz = sizeof(msg_hdr_t);
    if (depth < 2) {
        fprintf(stderr, "[T1] WARNING: depth=%ld < 2. Priority cannot show"
                " effect with depth=1. Forcing depth=8.\n", depth);
        depth = 8;
    }

    uint64_t total = (uint64_t)n * 2;  /* n LOW + n HIGH */

    char qname[64];
    snprintf(qname, sizeof(qname), "%s_t1", Q_BASE);
    mq_unlink(qname);

    struct mq_attr attr = { .mq_maxmsg = depth, .mq_msgsize = (long)msz };
    mqd_t wq = mq_open(qname, O_CREAT | O_WRONLY, 0600, &attr);
    mqd_t rq = mq_open(qname, O_RDONLY);
    if (wq == (mqd_t)-1 || rq == (mqd_t)-1) {
        perror("mq_open T1");
        fprintf(stderr,
            "[T1] Queue open failed for depth=%ld msg_size=%zu.\n"
            "  If errno=EINVAL: sudo sysctl fs.mqueue.msgsize_max=%zu\n"
            "  depth=%ld * (msg_size=%zu + 48 overhead) = %ld bytes needed,\n"
            "  RLIMIT_MSGQUEUE default is 819200 bytes.\n",
            depth, msz, msz, depth, msz, depth * (long)(msz + 48));
        return 1;
    }

    /* Pipe for consumer to return recv_sample_t array to parent */
    int pfd[2];
    if (pipe(pfd) != 0) { perror("pipe T1"); return 1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork T1"); return 1; }

    if (pid == 0) {
        /* ── Consumer child ─────────────────────────────────────────────── */
        close(pfd[0]);
        mq_close(wq);
        if (cpu1 >= 0) pin_to_cpu(cpu1);

        char *buf = malloc(msz);
        recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
        if (!buf || !smp) { perror("malloc consumer T1"); exit(1); }

        for (uint32_t i = 0; i < (uint32_t)total; i++) {
            unsigned int recv_prio = 0;
            if (mq_receive(rq, buf, msz, &recv_prio) < 0) {
                perror("mq_receive T1"); exit(1);
            }
            uint64_t recv_ns = now_ns();
            msg_hdr_t h;
            memcpy(&h, buf, sizeof(h));
            smp[i].latency_ns    = (recv_ns > h.send_ns) ? recv_ns - h.send_ns : 0;
            smp[i].seqno         = h.seqno;
            smp[i].recv_position = i;
            smp[i].priority      = (uint8_t)recv_prio;
        }

        /* Send all samples back */
        size_t rem = total * sizeof(recv_sample_t);
        const char *ptr = (const char *)smp;
        while (rem > 0) {
            ssize_t w = write(pfd[1], ptr, rem);
            if (w <= 0) { perror("pipe write T1"); exit(1); }
            ptr += w; rem -= (size_t)w;
        }
        free(buf); free(smp);
        mq_close(rq);
        close(pfd[1]);
        exit(0);
    }

    /* ── Producer (parent): alternating LOW / HIGH in tight loop ──────── */
    close(pfd[1]);
    mq_close(rq);
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    char *buf = malloc(msz);
    if (!buf) { perror("malloc producer T1"); return 1; }

    uint64_t t0 = now_ns();
    for (uint32_t i = 0; i < n; i++) {
        /* Send LOW first, then HIGH — both with the same seqno pair */
        fill_msg(buf, msz, now_ns(), 2*i,   PRIO_LOW);
        if (mq_send(wq, buf, msz, PRIO_LOW)  != 0) { perror("mq_send T1 L"); return 1; }
        fill_msg(buf, msz, now_ns(), 2*i+1, PRIO_HIGH);
        if (mq_send(wq, buf, msz, PRIO_HIGH) != 0) { perror("mq_send T1 H"); return 1; }
    }
    uint64_t elapsed = now_ns() - t0;
    free(buf);

    /* Drain pipe */
    recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
    if (!smp) { perror("malloc smp T1"); return 1; }
    {
        size_t rem = total * sizeof(recv_sample_t);
        char *ptr  = (char *)smp;
        while (rem > 0) {
            ssize_t got = read(pfd[0], ptr, rem);
            if (got <= 0) break;
            ptr += got; rem -= (size_t)got;
        }
    }
    close(pfd[0]);
    waitpid(pid, NULL, 0);
    mq_close(wq); mq_unlink(qname);

    /* ── Compute jump distances ──────────────────────────────────────────
     *
     * For each received message, its seqno tells us its FIFO arrival order.
     * recv_position tells us when it was actually delivered.
     * jump = seqno - recv_position (positive = delivered earlier than FIFO)
     *
     * A HIGH message with seqno=5 delivered at recv_position=3 has jump=2:
     * it skipped 2 LOW messages that arrived before it.
     */
    int64_t  total_jump_high = 0;
    uint64_t n_high = 0, n_low = 0;
    uint64_t *lat_high = alloc_samples(n);
    uint64_t *lat_low  = alloc_samples(n);
    int64_t   max_jump = 0;

    for (uint64_t i = 0; i < total; i++) {
        int64_t jump = (int64_t)smp[i].seqno - (int64_t)smp[i].recv_position;
        if (smp[i].priority == PRIO_HIGH) {
            if (n_high < n) lat_high[n_high] = smp[i].latency_ns;
            n_high++;
            total_jump_high += jump;
            if (jump > max_jump) max_jump = jump;
        } else {
            if (n_low < n) lat_low[n_low] = smp[i].latency_ns;
            n_low++;
        }
    }

    double avg_jump = n_high > 0 ? (double)total_jump_high / n_high : 0.0;
    /* Theoretical maximum jump if POSIX MQ delivers all HIGH before any LOW:
     * HIGH message i (seqno=2i+1) is received at position i → jump=i+1.
     * Average = (1+2+...+n)/n = (n+1)/2.
     * Efficiency = avg_jump / theoretical_max — 1.0 = perfect priority.  */
    double theoretical_max = (n + 1) / 2.0;
    double efficiency      = (theoretical_max > 0) ? avg_jump / theoretical_max : 0.0;

    fprintf(stderr,
            "[T1] depth=%ld  HIGH avg_jump=%.2f  theoretical_max=%.2f"
            "  efficiency=%.4f  max_single_jump=%"PRId64
            "  (n_high=%"PRIu64" n_low=%"PRIu64")\n",
            depth, avg_jump, theoretical_max, efficiency,
            max_jump, n_high, n_low);

    /* Emit two latency rows — one per priority */
    result_t r_high = { .samples=lat_high, .n=n_high, .msg_size=msz,
                        .elapsed_ns=elapsed, .run_id=run_id };
    result_t r_low  = { .samples=lat_low,  .n=n_low,  .msg_size=msz,
                        .elapsed_ns=elapsed, .run_id=run_id };
    compute_stats(&r_high);
    compute_stats(&r_low);
    print_csv_row_mech("mq_posix", "priority_t1_high", 2, label, &r_high);
    print_csv_row_mech("mq_posix", "priority_t1_low",  2, label, &r_low);

    /* Summary comment for analyze.py */
    printf("# T1_jump,depth=%ld,avg_jump=%.2f,theoretical_max=%.2f"
           ",efficiency=%.4f,max_jump=%"PRId64
           ",n_high=%"PRIu64",n_low=%"PRIu64",run_id=%d\n",
           depth, avg_jump, theoretical_max, efficiency,
           max_jump, n_high, n_low, run_id);

    free(smp); free(lat_high); free(lat_low);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * T2 — Backpressure latency divergence
 * ══════════════════════════════════════════════════════════════════════════ */

static int run_t2(uint32_t n, size_t msz, long depth,
                  int cpu0, int cpu1, const char *label, int run_id)
{
    if (msz < sizeof(msg_hdr_t)) msz = sizeof(msg_hdr_t);
    if (depth < 2) depth = 8;

    uint64_t total = (uint64_t)n * 2;

    char qname[64];
    snprintf(qname, sizeof(qname), "%s_t2", Q_BASE);
    mq_unlink(qname);

    struct mq_attr attr = { .mq_maxmsg = depth, .mq_msgsize = (long)msz };
    mqd_t wq = mq_open(qname, O_CREAT | O_WRONLY, 0600, &attr);
    mqd_t rq = mq_open(qname, O_RDONLY);
    if (wq == (mqd_t)-1 || rq == (mqd_t)-1) {
        perror("mq_open T2");
        fprintf(stderr,
            "[T2] Queue open failed for depth=%ld msg_size=%zu.\n"
            "  depth=%ld * (msg_size=%zu + 48) = %ld bytes needed vs "
            "RLIMIT_MSGQUEUE default of 819200.\n",
            depth, msz, depth, msz, depth * (long)(msz + 48));
        return 1;
    }

    int pfd[2];
    if (pipe(pfd) != 0) { perror("pipe T2"); return 1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork T2"); return 1; }

    if (pid == 0) {
        /* ── Throttled consumer ─────────────────────────────────────────── */
        close(pfd[0]);
        mq_close(wq);
        if (cpu1 >= 0) pin_to_cpu(cpu1);

        char *buf = malloc(msz);
        recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
        if (!buf || !smp) { perror("malloc consumer T2"); exit(1); }

        for (uint32_t i = 0; i < (uint32_t)total; i++) {
            unsigned int recv_prio = 0;
            if (mq_receive(rq, buf, msz, &recv_prio) < 0) {
                perror("mq_receive T2"); exit(1);
            }
            uint64_t recv_ns = now_ns();
            msg_hdr_t h;
            memcpy(&h, buf, sizeof(h));
            smp[i].latency_ns    = (recv_ns > h.send_ns) ? recv_ns - h.send_ns : 0;
            smp[i].seqno         = h.seqno;
            smp[i].recv_position = i;
            smp[i].priority      = (uint8_t)recv_prio;

            /*
             * Throttle: sleep 1 µs between receives so the queue fills up.
             * This creates sustained backpressure — the producer always has
             * multiple messages queued, giving the kernel a real choice
             * between LOW and HIGH priorities at every mq_receive() call.
             * Without this sleep the consumer drains faster than the producer
             * fills, and priority has no effect on ordering.
             */
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000 };
            nanosleep(&ts, NULL);
        }

        size_t rem = total * sizeof(recv_sample_t);
        const char *ptr = (const char *)smp;
        while (rem > 0) {
            ssize_t w = write(pfd[1], ptr, rem);
            if (w <= 0) { perror("pipe write T2"); exit(1); }
            ptr += w; rem -= (size_t)w;
        }
        free(buf); free(smp);
        mq_close(rq);
        close(pfd[1]);
        exit(0);
    }

    /* ── Fast producer: alternating LOW/HIGH ───────────────────────────── */
    close(pfd[1]);
    mq_close(rq);
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    char *buf = malloc(msz);
    if (!buf) { perror("malloc producer T2"); return 1; }

    uint64_t t0 = now_ns();
    for (uint32_t i = 0; i < n; i++) {
        fill_msg(buf, msz, now_ns(), 2*i,   PRIO_LOW);
        if (mq_send(wq, buf, msz, PRIO_LOW)  != 0) { perror("mq_send T2 L"); return 1; }
        fill_msg(buf, msz, now_ns(), 2*i+1, PRIO_HIGH);
        if (mq_send(wq, buf, msz, PRIO_HIGH) != 0) { perror("mq_send T2 H"); return 1; }
    }
    uint64_t elapsed = now_ns() - t0;
    free(buf);

    recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
    if (!smp) { perror("malloc smp T2 parent"); return 1; }
    {
        size_t rem = total * sizeof(recv_sample_t);
        char *ptr  = (char *)smp;
        while (rem > 0) {
            ssize_t got = read(pfd[0], ptr, rem);
            if (got <= 0) break;
            ptr += got; rem -= (size_t)got;
        }
    }
    close(pfd[0]);
    waitpid(pid, NULL, 0);
    mq_close(wq); mq_unlink(qname);

    /* Split by priority */
    uint64_t *lat_high = alloc_samples(n);
    uint64_t *lat_low  = alloc_samples(n);
    uint64_t nh = 0, nl = 0;

    for (uint64_t i = 0; i < total; i++) {
        if (smp[i].priority == PRIO_HIGH && nh < n) lat_high[nh++] = smp[i].latency_ns;
        if (smp[i].priority == PRIO_LOW  && nl < n) lat_low[nl++]  = smp[i].latency_ns;
    }
    free(smp);

    result_t r_high = { .samples=lat_high, .n=nh, .msg_size=msz,
                        .elapsed_ns=elapsed, .run_id=run_id };
    result_t r_low  = { .samples=lat_low,  .n=nl, .msg_size=msz,
                        .elapsed_ns=elapsed, .run_id=run_id };
    compute_stats(&r_high);
    compute_stats(&r_low);
    /* Throughput is not meaningful for T2: consumer is intentionally
     * throttled at 1 µs/msg to create backpressure. Zero it out.   */
    r_high.throughput_msg_s = 0; r_high.throughput_MB_s = 0;
    r_low.throughput_msg_s  = 0; r_low.throughput_MB_s  = 0;

    fprintf(stderr,
            "[T2] backpressure depth=%ld  HIGH p99=%.0f ns  LOW p99=%.0f ns"
            "  ratio=%.2fx\n",
            depth, r_high.p99_ns, r_low.p99_ns,
            (r_high.p99_ns > 0) ? r_low.p99_ns / r_high.p99_ns : 0.0);

    print_csv_row_mech("mq_posix", "priority_t2_high", 2, label, &r_high);
    print_csv_row_mech("mq_posix", "priority_t2_low",  2, label, &r_low);

    free(lat_high); free(lat_low);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * T3 — PPR vs N-producer sweep with start barrier
 * ══════════════════════════════════════════════════════════════════════════ */

static int run_t3(int n_procs, uint32_t k, size_t msz,
                  const char *label, int run_id)
{
    if (msz < sizeof(msg_hdr_t)) msz = sizeof(msg_hdr_t);

    uint64_t total = (uint64_t)n_procs * k;

    /*
     * Queue depth: large enough that producers rarely block on mq_send(),
     * forcing the consumer to select among messages of different priorities.
     * Cap at 1024 (kernel default msg_max).
     */
    long depth = (long)n_procs * 16;
    if (depth > 1024) depth = 1024;

    char qname[64];
    snprintf(qname, sizeof(qname), "%s_t3", Q_BASE);
    mq_unlink(qname);

    /* Open one write descriptor per producer + consumer read descriptor.
     * Apply the same depth-degradation strategy as throughput.c in case
     * n_procs * 16 * msg_size exceeds RLIMIT_MSGQUEUE.               */
    mqd_t *wqs = malloc(n_procs * sizeof(mqd_t));
    if (!wqs) { perror("malloc wqs"); return 1; }

    long actual_depth = depth;
    while (actual_depth >= 1) {
        struct mq_attr attr2 = { .mq_maxmsg = actual_depth,
                                 .mq_msgsize = (long)msz };
        mq_unlink(qname);
        wqs[0] = mq_open(qname, O_CREAT | O_WRONLY, 0600, &attr2);
        if (wqs[0] != (mqd_t)-1) { depth = actual_depth; break; }
        if (errno == EACCES || errno == EEXIST || actual_depth == 1) {
            perror("mq_open T3 create");
            fprintf(stderr, "[T3] Try: sudo sysctl fs.mqueue.msg_max=%ld\n",
                    actual_depth);
            free(wqs); return 1;
        }
        actual_depth /= 2;
        fprintf(stderr, "[T3] depth degraded to %ld due to resource limit\n",
                actual_depth);
    }
    for (int i = 1; i < n_procs; i++) {
        wqs[i] = mq_open(qname, O_WRONLY);
        if (wqs[i] == (mqd_t)-1) { perror("mq_open T3 wq"); free(wqs); return 1; }
    }
    mqd_t rq = mq_open(qname, O_RDONLY);
    if (rq == (mqd_t)-1) { perror("mq_open T3 rq"); free(wqs); return 1; }

    /*
     * Start barrier: one pipe, parent writes one byte per producer.
     * Each producer blocks on read() until the parent fires.
     * This ensures all N producers start sending simultaneously,
     * creating genuine concurrent contention.
     */
    int barrier[2];
    if (pipe(barrier) != 0) { perror("pipe barrier T3"); free(wqs); return 1; }

    /* Done-signal pipes: each producer writes 1 byte when finished.
     * Parent uses this to sequence waitpid() safely.               */
    int (*spipe)[2] = malloc(n_procs * sizeof(*spipe));
    if (!spipe) { perror("malloc spipe"); free(wqs); return 1; }
    for (int i = 0; i < n_procs; i++) {
        if (pipe(spipe[i]) != 0) { perror("pipe spipe T3"); return 1; }
    }

    pid_t *pids = malloc(n_procs * sizeof(pid_t));
    if (!pids) { perror("malloc pids"); free(wqs); return 1; }

    /* Fork N producers */
    for (int i = 0; i < n_procs; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork T3"); return 1; }
        if (pids[i] == 0) {
            /* Close all other write ends */
            for (int j = 0; j < n_procs; j++) if (j != i) mq_close(wqs[j]);
            mq_close(rq);
            close(barrier[1]);        /* child only reads from barrier */
            for (int j = 0; j < n_procs; j++) {
                if (j != i) { close(spipe[j][0]); close(spipe[j][1]); }
                else         { close(spipe[j][0]); }  /* keep write end only */
            }

            pin_to_cpu(i % num_cpus());

            /* Wait for start signal — blocks until parent fires barrier */
            char go;
            if (read(barrier[0], &go, 1) != 1) {
                perror("barrier read T3"); exit(1);
            }
            close(barrier[0]);

            unsigned int my_prio = (unsigned int)(i + 1);
            char *buf = malloc(msz);
            if (!buf) { perror("malloc prod buf T3"); exit(1); }

            for (uint32_t m = 0; m < k; m++) {
                fill_msg(buf, msz, now_ns(), m, (uint8_t)my_prio);
                if (mq_send(wqs[i], buf, msz, my_prio) != 0) {
                    perror("mq_send T3"); exit(1);
                }
            }
            free(buf);
            mq_close(wqs[i]);
            /* Signal parent that this producer is done */
            char done_byte = 1;
            { ssize_t _w = write(spipe[i][1], &done_byte, 1); (void)_w; }
            close(spipe[i][1]);
            exit(0);
        }
        mq_close(wqs[i]);
    }
    free(wqs);

    /* Parent = consumer */
    close(barrier[0]);                              /* parent only writes to barrier */
    for (int i = 0; i < n_procs; i++) close(spipe[i][1]);

    pin_to_cpu(n_procs % num_cpus());

    char *buf         = malloc(msz);
    uint64_t *latencies = alloc_samples(total);
    if (!buf || !latencies) { perror("malloc consumer T3"); return 1; }

    /* Fire start barrier — all N producers unblock simultaneously */
    for (int i = 0; i < n_procs; i++) {
        char go = 1;
        if (write(barrier[1], &go, 1) != 1) {
            perror("barrier write T3"); return 1;
        }
    }
    close(barrier[1]);

    uint64_t  in_order  = 0;
    uint64_t  received  = 0;
    unsigned int last_prio = (unsigned int)(n_procs + 1);  /* start above all valid levels */

    mem_snapshot_t mem_before = mem_snapshot();
    uint64_t t0 = now_ns();

    /*
     * Blocking mq_receive — the queue was opened without O_NONBLOCK so this
     * blocks until a message arrives.  We drain exactly `total` messages.
     * Since all N producers send exactly k messages each and we only return
     * after receiving all of them, there is no early-exit race.
     */
    while (received < total) {
        unsigned int recv_prio = 0;
        ssize_t got = mq_receive(rq, buf, msz, &recv_prio);
        if (got < 0) {
            perror("mq_receive T3"); break;
        }
        msg_hdr_t h;
        memcpy(&h, buf, sizeof(h));
        uint64_t recv_ns = now_ns();
        latencies[received] = (recv_ns > h.send_ns) ? recv_ns - h.send_ns : 0;

        /* Count messages received in non-increasing priority order */
        if (recv_prio <= last_prio) in_order++;
        last_prio = recv_prio;
        received++;
    }

    uint64_t elapsed = now_ns() - t0;
    mem_snapshot_t mem_after = mem_snapshot();

    /* Drain done-signals and reap children */
    for (int i = 0; i < n_procs; i++) {
        char done;
        { ssize_t _r = read(spipe[i][0], &done, 1); (void)_r; }
        close(spipe[i][0]);
        waitpid(pids[i], NULL, 0);
    }
    mq_close(rq); mq_unlink(qname);

    double ppr = (received > 0) ? (double)in_order / (double)received : 0.0;

    fprintf(stderr,
            "[T3] n_procs=%d k=%u depth=%ld: %"PRIu64"/%"PRIu64
            " in-order (PPR=%.4f)\n",
            n_procs, k, depth, in_order, received, ppr);

    result_t r = {
        .samples      = latencies,
        .n            = received,
        .msg_size     = msz,
        .elapsed_ns   = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id       = run_id,
    };
    compute_stats(&r);
    /* Throughput not meaningful here — N producers, varied priorities.
     * Zero it out so analysis script does not misinterpret.          */
    r.throughput_msg_s = 0;
    r.throughput_MB_s  = 0;

    char bench[32];
    snprintf(bench, sizeof(bench), "priority_t3_n%d", n_procs);
    print_csv_row_mech("mq_posix", bench, n_procs + 1, label, &r);

    printf("# T3_ppr,n_procs=%d,k=%u,depth=%ld,in_order=%"PRIu64
           ",total=%"PRIu64",ppr=%.6f,run_id=%d\n",
           n_procs, k, depth, in_order, received, ppr, run_id);

    free(buf); free(latencies); free(pids); free(spipe);
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int         test    = 0;
    uint32_t    n       = 5000;
    size_t      msz     = 64;
    long        depth   = 8;
    int         n_procs = 4;
    int         cpu0    = -1, cpu1 = -1;
    const char *label   = "unspecified";
    int         run_id  = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "t:n:s:d:N:c:p:r:H")) != -1) {
        switch (opt) {
        case 't': test    = (int)strtol(optarg, NULL, 10);       break;
        case 'n': n       = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 's': msz     = strtoul(optarg, NULL, 10);            break;
        case 'd': depth   = strtol(optarg, NULL, 10);             break;
        case 'N': n_procs = (int)strtol(optarg, NULL, 10);       break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1);         break;
        case 'p': label   = optarg;                                break;
        case 'r': run_id  = (int)strtol(optarg, NULL, 10);       break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr,
                    "usage: priority_test -t <1|2|3> [-n msgs] [-s bytes]\n"
                    "                     [-d depth] [-N procs]\n"
                    "                     [-c cpu0,cpu1] [-p label]\n"
                    "                     [-r run_id] [-H]\n");
            return 1;
        }
    }

    if (test < 1 || test > 3) {
        fprintf(stderr, "error: -t is required (1, 2, or 3)\n");
        return 1;
    }

    switch (test) {
    case 1: return run_t1(n, msz, depth, cpu0, cpu1, label, run_id);
    case 2: return run_t2(n, msz, depth, cpu0, cpu1, label, run_id);
    case 3: return run_t3(n_procs, n, msz, label, run_id);
    }
    return 0;
}
