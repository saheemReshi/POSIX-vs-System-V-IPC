/*
 * shm_throughput.c — Producer→Consumer Throughput via POSIX Shared Memory
 *
 * Single-producer / single-consumer ring buffer over a POSIX shm object.
 * Synchronisation: two named POSIX semaphores
 *   /ipc_shm_tp_empty  initial=depth (free slots available)
 *   /ipc_shm_tp_full   initial=0     (filled slots available)
 *
 * Payload never traverses the kernel — it is memcpy'd directly into the
 * shared mapping. Each message therefore costs:
 *   producer:  sem_wait(empty) + memcpy + sem_post(full)
 *   consumer:  sem_wait(full)  + memcpy + sem_post(empty)
 * That is 4 sem syscalls per message (2 each side) vs 2 mq syscalls in
 * mq_posix throughput, with the message-copy moved out of the kernel.
 *
 * Producer-side wall clock is the headline metric (matches mq_posix).
 *
 * Usage: shm_throughput [-n msgs] [-s bytes] [-q depth] [-c cpu0,cpu1]
 *                       [-p label] [-r run_id] [-H]
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

#define SHM_NAME    "/ipc_shm_tp"
#define SEM_EMPTY   "/ipc_shm_tp_empty"
#define SEM_FULL    "/ipc_shm_tp_full"

#define WARMUP_FRAC      0.05   /* 5% of iterations as warmup */
#define WARMUP_MAX       5000

static void run_producer(volatile char *ring, size_t msz, long depth,
                          sem_t *sem_empty, sem_t *sem_full,
                          uint64_t n)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    memset(buf, 0xAB, msz);

    long head = 0;
    for (uint64_t i = 0; i < n; i++) {
        buf[0] = (char)(i & 0xFF);

        while (sem_wait(sem_empty) != 0) {
            if (errno != EINTR) { perror("sem_wait empty"); exit(1); }
        }
        memcpy((void *)(ring + (size_t)head * msz), buf, msz);
        head = (head + 1) % depth;
        if (sem_post(sem_full) != 0) { perror("sem_post full"); exit(1); }
    }
    free(buf);
}

static void run_consumer(volatile char *ring, size_t msz, long depth,
                          sem_t *sem_empty, sem_t *sem_full,
                          uint64_t n)
{
    char *buf = malloc(msz);
    if (!buf) { perror("malloc"); exit(1); }
    volatile char sink = 0;

    long tail = 0;
    for (uint64_t i = 0; i < n; i++) {
        while (sem_wait(sem_full) != 0) {
            if (errno != EINTR) { perror("sem_wait full"); exit(1); }
        }
        memcpy(buf, (const void *)(ring + (size_t)tail * msz), msz);
        sink += buf[0];
        tail = (tail + 1) % depth;
        if (sem_post(sem_empty) != 0) { perror("sem_post empty"); exit(1); }
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

    /* Warmup is folded into n_msgs — caller doesn't know.  We run an extra
     * `warmup` messages first that are not used in the timing window. */
    uint64_t warmup = (uint64_t)((double)n_msgs * WARMUP_FRAC);
    if (warmup > WARMUP_MAX) warmup = WARMUP_MAX;

    /* Shared ring sized for `depth` slots */
    size_t shm_size = (size_t)depth * msz;

    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, (off_t)shm_size) != 0) { perror("ftruncate"); return 1; }

    volatile char *ring = mmap(NULL, shm_size,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    sem_unlink(SEM_EMPTY); sem_unlink(SEM_FULL);
    sem_t *sem_empty = sem_open(SEM_EMPTY, O_CREAT | O_EXCL, 0600,
                                 (unsigned int)depth);
    sem_t *sem_full  = sem_open(SEM_FULL,  O_CREAT | O_EXCL, 0600, 0);
    if (sem_empty == SEM_FAILED || sem_full == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* Child = consumer */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        if (warmup) run_consumer(ring, msz, depth, sem_empty, sem_full, warmup);
        run_consumer(ring, msz, depth, sem_empty, sem_full, n_msgs);
        sem_close(sem_empty); sem_close(sem_full);
        munmap((void *)ring, shm_size);
        exit(0);
    }

    /* Parent = producer */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    /* Warmup outside timing window */
    if (warmup) run_producer(ring, msz, depth, sem_empty, sem_full, warmup);

    uint64_t t0 = now_ns();
    run_producer(ring, msz, depth, sem_empty, sem_full, n_msgs);
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
    print_csv_row_mech("shm_posix", "throughput", 2, label, &r);

    sem_close(sem_empty); sem_unlink(SEM_EMPTY);
    sem_close(sem_full);  sem_unlink(SEM_FULL);
    munmap((void *)ring, shm_size);
    shm_unlink(SHM_NAME);
    return 0;
}
