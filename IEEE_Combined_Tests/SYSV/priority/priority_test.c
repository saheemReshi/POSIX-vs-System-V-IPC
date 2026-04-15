/*
 * priority_test.c — System V MQ priority behavior benchmarks
 *
 * System V queues do not have a native per-message priority field like POSIX
 * mq_send(..., prio). Here we emulate two-level priority using mtype and
 * msgrcv selector semantics:
 *   - smaller mtype => higher priority
 *   - msgrcv(..., msgtyp < 0) receives the message with the lowest type
 *
 * Tests are API-compatible with POSIX priority_test so orchestration and
 * analysis can consume rows with the same benchmark names.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

#define PRIO_HIGH 1L
#define PRIO_LOW  30L

#define KEY_T1 ((key_t)0x7A1001)
#define KEY_T2 ((key_t)0x7A1002)
#define KEY_T3 ((key_t)0x7A1003)

typedef struct {
    uint64_t send_ns;
    uint32_t seqno;
    uint8_t priority;
    uint8_t pad[3];
} msg_hdr_t;

typedef struct {
    long mtype;
    char mtext[1];
} sv_msg_t;

typedef struct {
    uint64_t latency_ns;
    uint32_t seqno;
    uint32_t recv_position;
    uint8_t priority;
    uint8_t pad[3];
} recv_sample_t;

static sv_msg_t *alloc_msg(size_t msz)
{
    sv_msg_t *m = malloc(sizeof(long) + msz);
    if (!m) { perror("malloc msg"); exit(1); }
    return m;
}

static void fill_msg(sv_msg_t *m, size_t msz, uint64_t ts,
                     uint32_t seq, uint8_t prio)
{
    msg_hdr_t h = {
        .send_ns = ts,
        .seqno = seq,
        .priority = prio,
        .pad = {0, 0, 0},
    };
    memcpy(m->mtext, &h, sizeof(h));
    if (msz > sizeof(h)) memset(m->mtext + sizeof(h), (int)prio, msz - sizeof(h));
}

static int remove_queue_by_key(key_t key)
{
    int id = msgget(key, 0);
    if (id >= 0) {
        if (msgctl(id, IPC_RMID, NULL) != 0) {
            perror("msgctl IPC_RMID");
            return -1;
        }
    }
    return 0;
}

static int create_queue_with_depth(key_t key, size_t msz, long req_depth,
                                   long *actual_depth)
{
    if (req_depth < 1) req_depth = 1;

    long depth = req_depth;
    while (depth >= 1) {
        if (remove_queue_by_key(key) != 0 && errno != ENOENT) return -1;

        int qid = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
        if (qid < 0) {
            perror("msgget");
            return -1;
        }

        struct msqid_ds ds;
        if (msgctl(qid, IPC_STAT, &ds) != 0) {
            perror("msgctl IPC_STAT");
            msgctl(qid, IPC_RMID, NULL);
            return -1;
        }

        ds.msg_qbytes = (unsigned long)depth * (sizeof(long) + msz);
        if (msgctl(qid, IPC_SET, &ds) == 0) {
            if (actual_depth) *actual_depth = depth;
            return qid;
        }

        if (depth == 1 || (errno != EACCES && errno != EPERM && errno != EINVAL &&
                           errno != ENOMEM && errno != ENOSPC)) {
            perror("msgctl IPC_SET");
            msgctl(qid, IPC_RMID, NULL);
            return -1;
        }

        msgctl(qid, IPC_RMID, NULL);
        depth /= 2;
    }

    return -1;
}

static int recv_with_priority(int qid, sv_msg_t *m, size_t msz, long selector,
                              unsigned int *recv_type)
{
    ssize_t rc;
    do {
        rc = msgrcv(qid, m, msz, selector, 0);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        perror("msgrcv");
        return -1;
    }

    if (recv_type) *recv_type = (unsigned int)m->mtype;
    return 0;
}

static int run_t1(uint32_t n, size_t msz, long depth,
                  int cpu0, int cpu1, const char *label, int run_id)
{
    if (msz < sizeof(msg_hdr_t)) msz = sizeof(msg_hdr_t);
    if (depth < 2) {
        fprintf(stderr, "[T1] WARNING: depth=%ld < 2. Forcing depth=8.\n", depth);
        depth = 8;
    }

    long actual_depth = depth;
    int qid = create_queue_with_depth(KEY_T1, msz, depth, &actual_depth);
    if (qid < 0) return 1;

    uint64_t total = (uint64_t)n * 2;
    int pfd[2];
    if (pipe(pfd) != 0) {
        perror("pipe T1");
        msgctl(qid, IPC_RMID, NULL);
        return 1;
    }

    mem_snapshot_t mem_before = mem_snapshot();

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork T1");
        close(pfd[0]); close(pfd[1]);
        msgctl(qid, IPC_RMID, NULL);
        return 1;
    }

    if (pid == 0) {
        close(pfd[0]);
        if (cpu1 >= 0) pin_to_cpu(cpu1);

        sv_msg_t *msg = alloc_msg(msz);
        recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
        if (!smp) { perror("malloc smp T1 child"); exit(1); }

        for (uint32_t i = 0; i < (uint32_t)total; i++) {
            unsigned int recv_type = 0;
            if (recv_with_priority(qid, msg, msz, -PRIO_LOW, &recv_type) != 0) exit(1);

            msg_hdr_t h;
            memcpy(&h, msg->mtext, sizeof(h));
            uint64_t recv_ns = now_ns();

            smp[i].latency_ns = (recv_ns > h.send_ns) ? (recv_ns - h.send_ns) : 0;
            smp[i].seqno = h.seqno;
            smp[i].recv_position = i;
            smp[i].priority = (uint8_t)recv_type;
        }

        size_t rem = total * sizeof(recv_sample_t);
        const char *ptr = (const char *)smp;
        while (rem > 0) {
            ssize_t w = write(pfd[1], ptr, rem);
            if (w <= 0) { perror("pipe write T1"); exit(1); }
            ptr += w; rem -= (size_t)w;
        }

        free(msg);
        free(smp);
        close(pfd[1]);
        exit(0);
    }

    close(pfd[1]);
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    sv_msg_t *msg = alloc_msg(msz);
    uint64_t t0 = now_ns();
    for (uint32_t i = 0; i < n; i++) {
        fill_msg(msg, msz, now_ns(), 2 * i, (uint8_t)PRIO_LOW);
        msg->mtype = PRIO_LOW;
        if (msgsnd(qid, msg, msz, 0) != 0) { perror("msgsnd T1 LOW"); return 1; }

        fill_msg(msg, msz, now_ns(), 2 * i + 1, (uint8_t)PRIO_HIGH);
        msg->mtype = PRIO_HIGH;
        if (msgsnd(qid, msg, msz, 0) != 0) { perror("msgsnd T1 HIGH"); return 1; }
    }
    uint64_t elapsed = now_ns() - t0;
    free(msg);

    recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
    if (!smp) {
        perror("malloc smp T1");
        close(pfd[0]);
        waitpid(pid, NULL, 0);
        msgctl(qid, IPC_RMID, NULL);
        return 1;
    }

    size_t rem = total * sizeof(recv_sample_t);
    char *ptr = (char *)smp;
    while (rem > 0) {
        ssize_t got = read(pfd[0], ptr, rem);
        if (got <= 0) break;
        ptr += got; rem -= (size_t)got;
    }

    close(pfd[0]);
    waitpid(pid, NULL, 0);
    mem_snapshot_t mem_after = mem_snapshot();
    msgctl(qid, IPC_RMID, NULL);

    int64_t total_jump_high = 0;
    int64_t max_jump = 0;
    uint64_t n_high = 0, n_low = 0;
    uint64_t *lat_high = alloc_samples(n);
    uint64_t *lat_low = alloc_samples(n);

    for (uint64_t i = 0; i < total; i++) {
        int64_t jump = (int64_t)smp[i].seqno - (int64_t)smp[i].recv_position;
        if (smp[i].priority == (uint8_t)PRIO_HIGH) {
            if (n_high < n) lat_high[n_high] = smp[i].latency_ns;
            n_high++;
            total_jump_high += jump;
            if (jump > max_jump) max_jump = jump;
        } else {
            if (n_low < n) lat_low[n_low] = smp[i].latency_ns;
            n_low++;
        }
    }

    double avg_jump = n_high ? (double)total_jump_high / (double)n_high : 0.0;
    double theoretical_max = (n + 1) / 2.0;
    double efficiency = (theoretical_max > 0) ? avg_jump / theoretical_max : 0.0;

    fprintf(stderr,
            "[T1] depth=%ld(actual=%ld) HIGH avg_jump=%.2f theoretical_max=%.2f efficiency=%.4f max_jump=%" PRId64 "\n",
            depth, actual_depth, avg_jump, theoretical_max, efficiency, max_jump);

    result_t r_high = {
        .samples = lat_high,
        .n = n_high,
        .msg_size = msz,
        .elapsed_ns = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id = run_id,
    };
    result_t r_low = {
        .samples = lat_low,
        .n = n_low,
        .msg_size = msz,
        .elapsed_ns = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id = run_id,
    };
    compute_stats(&r_high);
    compute_stats(&r_low);

    print_csv_row_mech("mq_sysv", "priority_t1_high", 2, label, &r_high);
    print_csv_row_mech("mq_sysv", "priority_t1_low", 2, label, &r_low);

    printf("# T1_jump,depth=%ld,actual_depth=%ld,avg_jump=%.2f,theoretical_max=%.2f,efficiency=%.4f,max_jump=%" PRId64 ",n_high=%" PRIu64 ",n_low=%" PRIu64 ",run_id=%d\n",
           depth, actual_depth, avg_jump, theoretical_max, efficiency,
           max_jump, n_high, n_low, run_id);

    free(smp);
    free(lat_high);
    free(lat_low);
    return 0;
}

static int run_t2(uint32_t n, size_t msz, long depth,
                  int cpu0, int cpu1, const char *label, int run_id)
{
    if (msz < sizeof(msg_hdr_t)) msz = sizeof(msg_hdr_t);
    if (depth < 2) depth = 8;

    long actual_depth = depth;
    int qid = create_queue_with_depth(KEY_T2, msz, depth, &actual_depth);
    if (qid < 0) return 1;

    uint64_t total = (uint64_t)n * 2;
    int pfd[2];
    if (pipe(pfd) != 0) {
        perror("pipe T2");
        msgctl(qid, IPC_RMID, NULL);
        return 1;
    }

    mem_snapshot_t mem_before = mem_snapshot();

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork T2");
        close(pfd[0]); close(pfd[1]);
        msgctl(qid, IPC_RMID, NULL);
        return 1;
    }

    if (pid == 0) {
        close(pfd[0]);
        if (cpu1 >= 0) pin_to_cpu(cpu1);

        sv_msg_t *msg = alloc_msg(msz);
        recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
        if (!smp) { perror("malloc smp T2 child"); exit(1); }

        for (uint32_t i = 0; i < (uint32_t)total; i++) {
            unsigned int recv_type = 0;
            if (recv_with_priority(qid, msg, msz, -PRIO_LOW, &recv_type) != 0) exit(1);

            msg_hdr_t h;
            memcpy(&h, msg->mtext, sizeof(h));
            uint64_t recv_ns = now_ns();

            smp[i].latency_ns = (recv_ns > h.send_ns) ? (recv_ns - h.send_ns) : 0;
            smp[i].seqno = h.seqno;
            smp[i].recv_position = i;
            smp[i].priority = (uint8_t)recv_type;

            struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000};
            nanosleep(&ts, NULL);
        }

        size_t rem = total * sizeof(recv_sample_t);
        const char *ptr = (const char *)smp;
        while (rem > 0) {
            ssize_t w = write(pfd[1], ptr, rem);
            if (w <= 0) { perror("pipe write T2"); exit(1); }
            ptr += w; rem -= (size_t)w;
        }

        free(msg);
        free(smp);
        close(pfd[1]);
        exit(0);
    }

    close(pfd[1]);
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    sv_msg_t *msg = alloc_msg(msz);
    uint64_t t0 = now_ns();
    for (uint32_t i = 0; i < n; i++) {
        fill_msg(msg, msz, now_ns(), 2 * i, (uint8_t)PRIO_LOW);
        msg->mtype = PRIO_LOW;
        if (msgsnd(qid, msg, msz, 0) != 0) { perror("msgsnd T2 LOW"); return 1; }

        fill_msg(msg, msz, now_ns(), 2 * i + 1, (uint8_t)PRIO_HIGH);
        msg->mtype = PRIO_HIGH;
        if (msgsnd(qid, msg, msz, 0) != 0) { perror("msgsnd T2 HIGH"); return 1; }
    }
    uint64_t elapsed = now_ns() - t0;
    free(msg);

    recv_sample_t *smp = malloc(total * sizeof(recv_sample_t));
    if (!smp) {
        perror("malloc smp T2");
        close(pfd[0]);
        waitpid(pid, NULL, 0);
        msgctl(qid, IPC_RMID, NULL);
        return 1;
    }

    size_t rem = total * sizeof(recv_sample_t);
    char *ptr = (char *)smp;
    while (rem > 0) {
        ssize_t got = read(pfd[0], ptr, rem);
        if (got <= 0) break;
        ptr += got; rem -= (size_t)got;
    }

    close(pfd[0]);
    waitpid(pid, NULL, 0);
    mem_snapshot_t mem_after = mem_snapshot();
    msgctl(qid, IPC_RMID, NULL);

    uint64_t *lat_high = alloc_samples(n);
    uint64_t *lat_low = alloc_samples(n);
    uint64_t nh = 0, nl = 0;

    for (uint64_t i = 0; i < total; i++) {
        if (smp[i].priority == (uint8_t)PRIO_HIGH && nh < n) lat_high[nh++] = smp[i].latency_ns;
        if (smp[i].priority == (uint8_t)PRIO_LOW && nl < n) lat_low[nl++] = smp[i].latency_ns;
    }

    result_t r_high = {
        .samples = lat_high,
        .n = nh,
        .msg_size = msz,
        .elapsed_ns = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id = run_id,
    };
    result_t r_low = {
        .samples = lat_low,
        .n = nl,
        .msg_size = msz,
        .elapsed_ns = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id = run_id,
    };
    compute_stats(&r_high);
    compute_stats(&r_low);

    r_high.throughput_msg_s = 0; r_high.throughput_MB_s = 0;
    r_low.throughput_msg_s = 0; r_low.throughput_MB_s = 0;

    fprintf(stderr,
            "[T2] depth=%ld(actual=%ld) HIGH p99=%.0f LOW p99=%.0f ratio=%.2fx\n",
            depth, actual_depth, r_high.p99_ns, r_low.p99_ns,
            (r_high.p99_ns > 0) ? (r_low.p99_ns / r_high.p99_ns) : 0.0);

    print_csv_row_mech("mq_sysv", "priority_t2_high", 2, label, &r_high);
    print_csv_row_mech("mq_sysv", "priority_t2_low", 2, label, &r_low);

    free(smp);
    free(lat_high);
    free(lat_low);
    return 0;
}

static int run_t3(int n_procs, uint32_t k, size_t msz,
                  const char *label, int run_id)
{
    if (n_procs < 1) n_procs = 1;
    if (msz < sizeof(msg_hdr_t)) msz = sizeof(msg_hdr_t);

    uint64_t total = (uint64_t)n_procs * k;

    long depth = (long)n_procs * 16;
    if (depth > 1024) depth = 1024;

    long actual_depth = depth;
    int qid = create_queue_with_depth(KEY_T3, msz, depth, &actual_depth);
    if (qid < 0) return 1;

    int barrier[2];
    if (pipe(barrier) != 0) {
        perror("pipe barrier T3");
        msgctl(qid, IPC_RMID, NULL);
        return 1;
    }

    int (*spipe)[2] = malloc((size_t)n_procs * sizeof(*spipe));
    pid_t *pids = malloc((size_t)n_procs * sizeof(pid_t));
    if (!spipe || !pids) {
        perror("malloc T3");
        close(barrier[0]); close(barrier[1]);
        msgctl(qid, IPC_RMID, NULL);
        free(spipe); free(pids);
        return 1;
    }

    for (int i = 0; i < n_procs; i++) {
        if (pipe(spipe[i]) != 0) {
            perror("pipe spipe T3");
            msgctl(qid, IPC_RMID, NULL);
            free(spipe); free(pids);
            return 1;
        }
    }

    for (int i = 0; i < n_procs; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork T3");
            msgctl(qid, IPC_RMID, NULL);
            free(spipe); free(pids);
            return 1;
        }

        if (pids[i] == 0) {
            close(barrier[1]);
            for (int j = 0; j < n_procs; j++) {
                if (j != i) {
                    close(spipe[j][0]);
                    close(spipe[j][1]);
                } else {
                    close(spipe[j][0]);
                }
            }

            pin_to_cpu(i % num_cpus());

            char go = 0;
            if (read(barrier[0], &go, 1) != 1) {
                perror("barrier read T3");
                exit(1);
            }
            close(barrier[0]);

            long my_type = (long)(i + 1);
            sv_msg_t *msg = alloc_msg(msz);
            for (uint32_t m = 0; m < k; m++) {
                fill_msg(msg, msz, now_ns(), m, (uint8_t)my_type);
                msg->mtype = my_type;
                if (msgsnd(qid, msg, msz, 0) != 0) {
                    perror("msgsnd T3");
                    exit(1);
                }
            }
            free(msg);

            char done = 1;
            { ssize_t _w = write(spipe[i][1], &done, 1); (void)_w; }
            close(spipe[i][1]);
            exit(0);
        }
    }

    close(barrier[0]);
    for (int i = 0; i < n_procs; i++) close(spipe[i][1]);

    pin_to_cpu(n_procs % num_cpus());

    sv_msg_t *msg = alloc_msg(msz);
    uint64_t *latencies = alloc_samples(total);

    for (int i = 0; i < n_procs; i++) {
        char go = 1;
        if (write(barrier[1], &go, 1) != 1) {
            perror("barrier write T3");
            return 1;
        }
    }
    close(barrier[1]);

    mem_snapshot_t mem_before = mem_snapshot();
    uint64_t t0 = now_ns();

    uint64_t received = 0;
    uint64_t in_order = 0;
    long last_type = 0;

    while (received < total) {
        if (recv_with_priority(qid, msg, msz, -(long)n_procs, NULL) != 0) break;

        msg_hdr_t h;
        memcpy(&h, msg->mtext, sizeof(h));
        uint64_t recv_ns = now_ns();
        latencies[received] = (recv_ns > h.send_ns) ? (recv_ns - h.send_ns) : 0;

        if (msg->mtype >= last_type) in_order++;
        last_type = msg->mtype;
        received++;
    }

    uint64_t elapsed = now_ns() - t0;
    mem_snapshot_t mem_after = mem_snapshot();

    for (int i = 0; i < n_procs; i++) {
        char done = 0;
        { ssize_t _r = read(spipe[i][0], &done, 1); (void)_r; }
        close(spipe[i][0]);
        waitpid(pids[i], NULL, 0);
    }

    msgctl(qid, IPC_RMID, NULL);

    double ppr = (received > 0) ? ((double)in_order / (double)received) : 0.0;

    fprintf(stderr,
            "[T3] n_procs=%d k=%u depth=%ld(actual=%ld): %" PRIu64 "/%" PRIu64 " in-order (PPR=%.4f)\n",
            n_procs, k, depth, actual_depth, in_order, received, ppr);

    result_t r = {
        .samples = latencies,
        .n = received,
        .msg_size = msz,
        .elapsed_ns = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id = run_id,
    };
    compute_stats(&r);
    r.throughput_msg_s = 0;
    r.throughput_MB_s = 0;

    char bench[32];
    snprintf(bench, sizeof(bench), "priority_t3_n%d", n_procs);
    print_csv_row_mech("mq_sysv", bench, n_procs + 1, label, &r);

    printf("# T3_ppr,n_procs=%d,k=%u,depth=%ld,actual_depth=%ld,in_order=%" PRIu64 ",total=%" PRIu64 ",ppr=%.6f,run_id=%d\n",
           n_procs, k, depth, actual_depth, in_order, received, ppr, run_id);

    free(msg);
    free(latencies);
    free(spipe);
    free(pids);
    return 0;
}

int main(int argc, char *argv[])
{
    int test = 0;
    uint32_t n = 5000;
    size_t msz = 64;
    long depth = 8;
    int n_procs = 4;
    int cpu0 = -1, cpu1 = -1;
    const char *label = "unspecified";
    int run_id = 0;
    int opt;

    while ((opt = getopt(argc, argv, "t:n:s:d:N:c:p:r:H")) != -1) {
        switch (opt) {
        case 't': test = (int)strtol(optarg, NULL, 10); break;
        case 'n': n = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 's': msz = strtoul(optarg, NULL, 10); break;
        case 'd': depth = strtol(optarg, NULL, 10); break;
        case 'N': n_procs = (int)strtol(optarg, NULL, 10); break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1); break;
        case 'p': label = optarg; break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10); break;
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
