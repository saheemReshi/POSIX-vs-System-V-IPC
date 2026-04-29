/*
 * shm_throughput.c — Producer→Consumer Throughput via System V Shared Memory
 *
 * Single-producer / single-consumer ring buffer over a System V shm segment.
 * Synchronisation: System V semaphore set with two members
 *   sem[0]  EMPTY  initial=depth
 *   sem[1]  FULL   initial=0
 *
 * Payload never traverses the kernel.  Each message costs:
 *   producer:  semop(EMPTY,-1) + memcpy + semop(FULL,+1)
 *   consumer:  semop(FULL,-1)  + memcpy + semop(EMPTY,+1)
 *
 * Mirrors POSIX shm_throughput exactly so latency/throughput differences
 * are attributable to the synchronisation primitive (named POSIX sem vs
 * SysV semop) — a useful direct comparison for the paper.
 *
 * Usage: shm_throughput [-n msgs] [-s bytes] [-q depth] [-c cpu0,cpu1]
 *                       [-p label] [-r run_id] [-H]
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

#define SHM_KEY ((key_t)0x7A5100)
#define SEM_KEY ((key_t)0x7A5101)

#define SEM_EMPTY 0
#define SEM_FULL  1

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

static void run_producer(volatile char *ring, size_t msz, long depth,
                          int semid, uint64_t n)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    memset(buf, 0xAB, msz);

    long head = 0;
    for (uint64_t i = 0; i < n; i++) {
        buf[0] = (char)(i & 0xFF);
        if (sem_op(semid, SEM_EMPTY, -1) != 0) exit(1);
        memcpy((void *)(ring + (size_t)head * msz), buf, msz);
        head = (head + 1) % depth;
        if (sem_op(semid, SEM_FULL, 1) != 0) exit(1);
    }
    free(buf);
}

static void run_consumer(volatile char *ring, size_t msz, long depth,
                          int semid, uint64_t n)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    volatile char sink = 0;

    long tail = 0;
    for (uint64_t i = 0; i < n; i++) {
        if (sem_op(semid, SEM_FULL, -1) != 0) exit(1);
        memcpy(buf, (const void *)(ring + (size_t)tail * msz), msz);
        sink += buf[0];
        tail = (tail + 1) % depth;
        if (sem_op(semid, SEM_EMPTY, 1) != 0) exit(1);
    }
    (void)sink;
    free(buf);
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
        case 'n': n_msgs = strtoull(optarg, NULL, 10);    break;
        case 's': msz    = strtoul(optarg, NULL, 10);     break;
        case 'q': depth  = strtol(optarg, NULL, 10);      break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1); break;
        case 'p': label  = optarg;                        break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10); break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr, "usage: shm_throughput [-n msgs] [-s bytes] "
                            "[-q depth] [-c cpu0,cpu1] [-p label] "
                            "[-r run_id] [-H]\n");
            return 1;
        }
    }

    if (msz == 0 || depth < 1) {
        fprintf(stderr, "msg size > 0 and depth >= 1 required\n");
        return 1;
    }

    uint64_t warmup = (uint64_t)((double)n_msgs * WARMUP_FRAC);
    if (warmup > WARMUP_MAX) warmup = WARMUP_MAX;

    size_t shm_size = (size_t)depth * msz;

    int stale_shm = shmget(SHM_KEY, 1, 0);
    if (stale_shm >= 0) shmctl(stale_shm, IPC_RMID, NULL);
    int stale_sem = semget(SEM_KEY, 2, 0);
    if (stale_sem >= 0) semctl(stale_sem, 0, IPC_RMID);

    int shmid = shmget(SHM_KEY, shm_size, IPC_CREAT | IPC_EXCL | 0600);
    if (shmid < 0) { perror("shmget"); return 1; }

    volatile char *ring = shmat(shmid, NULL, 0);
    if (ring == (void *)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    int semid = semget(SEM_KEY, 2, IPC_CREAT | IPC_EXCL | 0600);
    if (semid < 0) {
        perror("semget");
        shmdt((const void *)ring);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    union semun arg;
    unsigned short init_vals[2] = { (unsigned short)depth, 0 };
    arg.array = init_vals;
    if (semctl(semid, 0, SETALL, arg) < 0) {
        perror("semctl SETALL");
        semctl(semid, 0, IPC_RMID);
        shmdt((const void *)ring);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        if (warmup) run_consumer(ring, msz, depth, semid, warmup);
        run_consumer(ring, msz, depth, semid, n_msgs);
        shmdt((const void *)ring);
        exit(0);
    }

    if (cpu0 >= 0) pin_to_cpu(cpu0);

    if (warmup) run_producer(ring, msz, depth, semid, warmup);

    uint64_t t0 = now_ns();
    run_producer(ring, msz, depth, semid, n_msgs);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);

    double sec = (double)elapsed / 1e9;
    result_t r = {
        .n                = n_msgs,
        .msg_size         = msz,
        .elapsed_ns       = elapsed,
        .avg_ns           = (double)elapsed / (double)n_msgs,
        .throughput_msg_s = (double)n_msgs / sec,
        .throughput_MB_s  = (double)n_msgs * (double)msz
                             / sec / (1024.0 * 1024.0),
        .mem_delta_kb     = 0,
        .run_id           = run_id,
    };
    print_csv_row_mech("shm_sysv", "throughput", 2, label, &r);

    semctl(semid, 0, IPC_RMID);
    shmdt((const void *)ring);
    shmctl(shmid, IPC_RMID, NULL);
    return 0;
}
