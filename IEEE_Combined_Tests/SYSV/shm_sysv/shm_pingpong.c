/*
 * shm_pingpong.c — Ping-Pong Latency via System V Shared Memory + Semaphores
 *
 * Shared memory carries payload; two System V semaphores synchronize turns.
 * This removes kernel message-copy overhead and provides a copy-free baseline
 * against mq_sysv pingpong.
 *
 * Usage: shm_pingpong [-n iters] [-s bytes] [-c cpu0,cpu1] [-p label]
 *                     [-r run_id] [-H]
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

#define SHM_KEY ((key_t)0x7A5000)
#define SEM_KEY ((key_t)0x7A5001)

#define SEM_FWD 0
#define SEM_BWD 1

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
#ifdef __linux__
    struct seminfo *__buf;
#endif
};

static int sem_wait_intr(int semid, unsigned short sem_num)
{
    struct sembuf op = {
        .sem_num = sem_num,
        .sem_op = -1,
        .sem_flg = 0,
    };

    while (semop(semid, &op, 1) != 0) {
        if (errno == EINTR) continue;
        perror("semop wait");
        return -1;
    }
    return 0;
}

static int sem_post_once(int semid, unsigned short sem_num)
{
    struct sembuf op = {
        .sem_num = sem_num,
        .sem_op = 1,
        .sem_flg = 0,
    };
    if (semop(semid, &op, 1) != 0) {
        perror("semop post");
        return -1;
    }
    return 0;
}

static void run_p1(volatile char *shm, size_t msz, int semid,
                   uint64_t iters, uint64_t *samples)
{
    volatile char *ping_buf = shm;
    volatile char *pong_buf = shm + msz;

    for (uint64_t i = 0; i < iters; i++) {
        memset((void *)ping_buf, (int)(i & 0xFF), msz);

        uint64_t t0 = now_ns();
        if (sem_post_once(semid, SEM_FWD) != 0) exit(1);
        if (sem_wait_intr(semid, SEM_BWD) != 0) exit(1);
        samples[i] = now_ns() - t0;

        (void)pong_buf[0];
    }
}

static void run_p2(volatile char *shm, size_t msz, int semid, uint64_t iters)
{
    volatile char *ping_buf = shm;
    volatile char *pong_buf = shm + msz;

    for (uint64_t i = 0; i < iters; i++) {
        if (sem_wait_intr(semid, SEM_FWD) != 0) exit(1);
        memcpy((void *)pong_buf, (const void *)ping_buf, msz);
        if (sem_post_once(semid, SEM_BWD) != 0) exit(1);
    }
}

int main(int argc, char *argv[])
{
    uint64_t iters = 10000;
    size_t msz = 64;
    int cpu0 = -1, cpu1 = -1;
    const char *label = "unspecified";
    int run_id = 0;
    int opt;

    while ((opt = getopt(argc, argv, "n:s:c:p:r:H")) != -1) {
        switch (opt) {
        case 'n': iters = strtoull(optarg, NULL, 10); break;
        case 's': msz = strtoul(optarg, NULL, 10); break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1); break;
        case 'p': label = optarg; break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10); break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr,
                    "usage: shm_pingpong [-n iters] [-s bytes] "
                    "[-c cpu0,cpu1] [-p label] [-r run_id] [-H]\n");
            return 1;
        }
    }

    if (msz == 0) {
        fprintf(stderr, "msg size must be > 0\n");
        return 1;
    }

    size_t shm_size = 2 * msz;

    int stale_shm = shmget(SHM_KEY, 1, 0);
    if (stale_shm >= 0) shmctl(stale_shm, IPC_RMID, NULL);

    int stale_sem = semget(SEM_KEY, 2, 0);
    if (stale_sem >= 0) semctl(stale_sem, 0, IPC_RMID);

    int shmid = shmget(SHM_KEY, shm_size, IPC_CREAT | IPC_EXCL | 0600);
    if (shmid < 0) {
        perror("shmget");
        return 1;
    }

    volatile char *shm = shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    int semid = semget(SEM_KEY, 2, IPC_CREAT | IPC_EXCL | 0600);
    if (semid < 0) {
        perror("semget");
        shmdt((const void *)shm);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    union semun arg;
    unsigned short init_vals[2] = {0, 0};
    arg.array = init_vals;
    if (semctl(semid, 0, SETALL, arg) < 0) {
        perror("semctl SETALL");
        semctl(semid, 0, IPC_RMID);
        shmdt((const void *)shm);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    uint64_t *samples = alloc_samples(iters);
    mem_snapshot_t mem_before = mem_snapshot();

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        free(samples);
        semctl(semid, 0, IPC_RMID);
        shmdt((const void *)shm);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pid == 0) {
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_p2(shm, msz, semid, iters);
        shmdt((const void *)shm);
        exit(0);
    }

    if (cpu0 >= 0) pin_to_cpu(cpu0);

    uint64_t t0 = now_ns();
    run_p1(shm, msz, semid, iters, samples);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);
    mem_snapshot_t mem_after = mem_snapshot();

    for (uint64_t i = 0; i < iters; i++) samples[i] /= 2;

    result_t r = {
        .samples = samples,
        .n = iters,
        .msg_size = msz,
        .elapsed_ns = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id = run_id,
    };
    compute_stats(&r);

    double sec = (double)elapsed / 1e9;
    r.throughput_msg_s = (double)iters / sec;
    r.throughput_MB_s = ((double)iters * 2.0 * (double)msz) /
                        sec / (1024.0 * 1024.0);

    print_csv_row_mech("shm_sysv", "pingpong", 2, label, &r);

    free(samples);
    semctl(semid, 0, IPC_RMID);
    shmdt((const void *)shm);
    shmctl(shmid, IPC_RMID, NULL);
    return 0;
}
