/*
 * shm_pairs.c — Parallel-Pairs Scalability via System V Shared Memory
 *
 * N independent SysV-shm ping-pong pairs run concurrently.  Each pair has
 * its own shmget segment (2*msz bytes) and its own 2-element semget set.
 * Initiators record per-iteration RTTs and ship them to the parent via
 * pipes; the parent aggregates and reports p50 / p95 / p99 / p99.9 and CSV.
 *
 * Mirrors stacks/POSIX/shm_posix/shm_pairs.c so the two stacks can be
 * compared directly on the synchronisation primitive (POSIX named sem
 * vs SysV semop).
 *
 * Usage: shm_pairs [-n N] [-k iters] [-s bytes] [-p label] [-r run_id] [-H]
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

#define SHM_KEY_BASE  ((key_t)0x7A5200)
#define SEM_KEY_BASE  ((key_t)0x7A5300)

#define SEM_FWD 0
#define SEM_BWD 1

#define WARMUP_FRAC 0.05
#define WARMUP_MAX  5000

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
#ifdef __linux__
    struct seminfo *__buf;
#endif
};

typedef struct {
    int           shmid;
    int           semid;
    volatile char *shm;
} pair_t;

static int sem_op(int semid, unsigned short num, short val)
{
    struct sembuf op = { .sem_num = num, .sem_op = val, .sem_flg = 0 };
    while (semop(semid, &op, 1) != 0) {
        if (errno == EINTR) continue;
        perror("semop");
        return -1;
    }
    return 0;
}

static void responder(pair_t *p, size_t msz, uint64_t iters)
{
    volatile char *ping = p->shm;
    volatile char *pong = p->shm + msz;

    for (uint64_t i = 0; i < iters; i++) {
        if (sem_op(p->semid, SEM_FWD, -1) != 0) exit(1);
        memcpy((void *)pong, (const void *)ping, msz);
        if (sem_op(p->semid, SEM_BWD, 1) != 0) exit(1);
    }
}

static void initiator(pair_t *p, size_t msz, uint64_t iters,
                       int pipe_wr, uint64_t warmup)
{
    volatile char *ping = p->shm;
    volatile char *pong = p->shm + msz;

    for (uint64_t i = 0; i < warmup; i++) {
        memset((void *)ping, (int)(i & 0xFF), msz);
        if (sem_op(p->semid, SEM_FWD, 1) != 0) exit(1);
        if (sem_op(p->semid, SEM_BWD, -1) != 0) exit(1);
        (void)pong[0];
    }

    uint64_t *smp = alloc_samples(iters);
    for (uint64_t i = 0; i < iters; i++) {
        memset((void *)ping, (int)(i & 0xFF), msz);
        uint64_t t0 = now_ns();
        if (sem_op(p->semid, SEM_FWD, 1) != 0) exit(1);
        if (sem_op(p->semid, SEM_BWD, -1) != 0) exit(1);
        smp[i] = (now_ns() - t0) / 2;
        (void)pong[0];
    }

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

static int run_pairs(int n, uint64_t iters, size_t msz,
                      const char *label, int run_id)
{
    if (n < 1 || iters == 0 || msz == 0) {
        fprintf(stderr, "usage: shm_pairs -n N -k K -s bytes\n");
        return 1;
    }

    uint64_t warmup = (uint64_t)((double)iters * WARMUP_FRAC);
    if (warmup > WARMUP_MAX) warmup = WARMUP_MAX;

    pair_t *pairs = calloc((size_t)n, sizeof(pair_t));
    if (!pairs) { perror("calloc"); return 1; }
    int (*pipes)[2] = malloc((size_t)n * sizeof(*pipes));
    pid_t *resp_pids = malloc((size_t)n * sizeof(pid_t));
    pid_t *init_pids = malloc((size_t)n * sizeof(pid_t));
    if (!pipes || !resp_pids || !init_pids) {
        perror("malloc"); return 1;
    }

    size_t shm_size = 2 * msz;

    for (int p = 0; p < n; p++) {
        key_t shm_key = SHM_KEY_BASE + p;
        key_t sem_key = SEM_KEY_BASE + p;

        int stale_shm = shmget(shm_key, 1, 0);
        if (stale_shm >= 0) shmctl(stale_shm, IPC_RMID, NULL);
        int stale_sem = semget(sem_key, 2, 0);
        if (stale_sem >= 0) semctl(stale_sem, 0, IPC_RMID);

        pairs[p].shmid = shmget(shm_key, shm_size,
                                 IPC_CREAT | IPC_EXCL | 0600);
        if (pairs[p].shmid < 0) { perror("shmget"); return 1; }
        pairs[p].shm = shmat(pairs[p].shmid, NULL, 0);
        if (pairs[p].shm == (void *)-1) { perror("shmat"); return 1; }

        pairs[p].semid = semget(sem_key, 2, IPC_CREAT | IPC_EXCL | 0600);
        if (pairs[p].semid < 0) { perror("semget"); return 1; }

        union semun arg;
        unsigned short init_vals[2] = {0, 0};
        arg.array = init_vals;
        if (semctl(pairs[p].semid, 0, SETALL, arg) < 0) {
            perror("semctl SETALL"); return 1;
        }

        if (pipe(pipes[p]) != 0) { perror("pipe"); return 1; }
    }

    int n_cpus = num_cpus();
    uint64_t t0 = now_ns();

    for (int p = 0; p < n; p++) {
        resp_pids[p] = fork();
        if (resp_pids[p] < 0) { perror("fork resp"); return 1; }
        if (resp_pids[p] == 0) {
            for (int q = 0; q < n; q++) {
                if (q != p) { close(pipes[q][0]); close(pipes[q][1]); }
            }
            close(pipes[p][0]); close(pipes[p][1]);
            pin_to_cpu((2 * p + 1) % n_cpus);
            responder(&pairs[p], msz, warmup + iters);
            exit(0);
        }

        init_pids[p] = fork();
        if (init_pids[p] < 0) { perror("fork init"); return 1; }
        if (init_pids[p] == 0) {
            for (int q = 0; q < n; q++) {
                if (q != p) { close(pipes[q][0]); close(pipes[q][1]); }
            }
            close(pipes[p][0]);
            pin_to_cpu((2 * p) % n_cpus);
            initiator(&pairs[p], msz, iters, pipes[p][1], warmup);
            exit(0);
        }
    }

    for (int p = 0; p < n; p++) close(pipes[p][1]);

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
    result_t r = {
        .samples    = flat,
        .n          = total,
        .msg_size   = msz,
        .elapsed_ns = elapsed,
        .run_id     = run_id,
    };
    compute_stats(&r);
    r.throughput_msg_s = 0;
    r.throughput_MB_s  = 0;
    print_csv_row_mech("shm_sysv", bench, n * 2, label, &r);

    for (int p = 0; p < n; p++) {
        semctl(pairs[p].semid, 0, IPC_RMID);
        shmdt((const void *)pairs[p].shm);
        shmctl(pairs[p].shmid, IPC_RMID, NULL);
    }
    free(pairs); free(pipes); free(resp_pids); free(init_pids); free(flat);
    return 0;
}

int main(int argc, char *argv[])
{
    int          n      = 4;
    uint64_t     iters  = 5000;
    size_t       msz    = 64;
    const char  *label  = "unspecified";
    int          run_id = 0;
    int          opt;

    while ((opt = getopt(argc, argv, "n:k:s:p:r:H")) != -1) {
        switch (opt) {
        case 'n': n      = (int)strtol(optarg, NULL, 10);          break;
        case 'k': iters  = strtoull(optarg, NULL, 10);              break;
        case 's': msz    = strtoul(optarg, NULL, 10);                break;
        case 'p': label  = optarg;                                    break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10);            break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: shm_pairs [-n N] [-k K] [-s bytes] "
                            "[-p label] [-r run_id] [-H]\n");
            return 1;
        }
    }

    return run_pairs(n, iters, msz, label, run_id);
}
