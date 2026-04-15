/*
 * shm_pingpong.c — Ping-Pong Latency via POSIX Shared Memory + Semaphores
 *
 * This is the third IPC baseline required for an IEEE-level comparison
 * alongside POSIX MQ and System V MQ.  Shared memory is the theoretical
 * upper-bound of same-machine IPC: the payload is never copied into the
 * kernel — it lives in a page mapped into both processes.  Synchronisation
 * is provided by two POSIX named semaphores (one per direction), which are
 * the lightest-weight cross-process signalling primitive available without
 * a custom futex.
 *
 * Architecture:
 *
 *   Shared region layout  (in shm object "/ipc_shm_pp"):
 *     [0 .. msg_size-1]           — ping payload (P1 → P2)
 *     [msg_size .. 2*msg_size-1]  — pong payload (P2 → P1)
 *
 *   sem_fwd  ("/ipc_shm_pp_fwd"):  P1 posts, P2 waits — "data ready"
 *   sem_bwd  ("/ipc_shm_pp_bwd"):  P2 posts, P1 waits — "reply ready"
 *
 * Why named semaphores and not sem_init() in shared memory?
 *   sem_init() with pshared=1 works, but requires the semaphore to live
 *   in the shared region itself and is not universally supported.  Named
 *   semaphores are POSIX-portable and make the benchmark directly
 *   comparable to the named-queue approach of mq_posix/pingpong.c.
 *
 * Key measurement property:
 *   The shm approach eliminates the kernel message-copy path.  Any latency
 *   gap vs. POSIX MQ is therefore attributable to:
 *     (a) mq_send/mq_receive syscall overhead + internal locking, and
 *     (b) the mandatory double-copy (user→kernel, kernel→user) in MQ.
 *   This makes the shm baseline essential for attributing overhead.
 *
 * Memory footprint:
 *   VmRSS delta is measured in the parent.  The shared page is charged to
 *   both processes by the kernel; we capture the parent's view (which
 *   includes its own copy of the mapping) as a conservative lower bound.
 *
 * Usage: shm_pingpong [-n iters] [-s bytes] [-c cpu0,cpu1] [-p label]
 *                     [-r run_id] [-H]
 *
 * Output: CSV row compatible with mq_posix/pingpong.c (mechanism = shm_posix)
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <stdint.h>
#include <errno.h>

#include "../common/timing.h"
#include "../common/affinity.h"
#include "../common/stats.h"

#define SHM_NAME  "/ipc_shm_pp"
#define SEM_FWD   "/ipc_shm_pp_fwd"
#define SEM_BWD   "/ipc_shm_pp_bwd"

/* ── P1 (initiator) ─────────────────────────────────────────────────────── */

static void run_p1(volatile char *shm, size_t msz,
                   sem_t *sem_fwd, sem_t *sem_bwd,
                   uint64_t iters, uint64_t *samples)
{
    volatile char *ping_buf = shm;               /* P1 writes here  */
    volatile char *pong_buf = shm + msz;         /* P2 writes here  */

    for (uint64_t i = 0; i < iters; i++) {
        /* Write payload into the shared ping region */
        memset((void *)ping_buf, (int)(i & 0xFF), msz);

        uint64_t t0 = now_ns();

        /* Signal P2: data is ready */
        if (sem_post(sem_fwd) != 0) { perror("sem_post fwd"); exit(1); }
        /* Wait for P2's reply */
        while (sem_wait(sem_bwd) != 0) {
            if (errno != EINTR) { perror("sem_wait bwd"); exit(1); }
        }

        samples[i] = now_ns() - t0;  /* RTT */

        /* Consume reply to prevent dead-store elimination */
        (void)pong_buf[0];
    }
}

/* ── P2 (responder) ─────────────────────────────────────────────────────── */

static void run_p2(volatile char *shm, size_t msz,
                   sem_t *sem_fwd, sem_t *sem_bwd,
                   uint64_t iters)
{
    volatile char *ping_buf = shm;        /* P2 reads from here  */
    volatile char *pong_buf = shm + msz;  /* P2 writes here      */

    for (uint64_t i = 0; i < iters; i++) {
        /* Wait for P1's ping */
        while (sem_wait(sem_fwd) != 0) {
            if (errno != EINTR) { perror("sem_wait fwd"); exit(1); }
        }

        /* Echo back: copy ping into pong region */
        memcpy((void *)pong_buf, (const void *)ping_buf, msz);

        /* Signal P1: reply is ready */
        if (sem_post(sem_bwd) != 0) { perror("sem_post bwd"); exit(1); }
    }
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    uint64_t    iters  = 10000;
    size_t      msz    = 64;
    int         cpu0   = -1, cpu1 = -1;
    const char *label  = "unspecified";
    int         run_id = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "n:s:c:p:r:H")) != -1) {
        switch (opt) {
        case 'n': iters  = strtoull(optarg, NULL, 10);      break;
        case 's': msz    = strtoul(optarg, NULL, 10);        break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1);    break;
        case 'p': label  = optarg;                            break;
        case 'r': run_id = (int)strtol(optarg, NULL, 10);   break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr,
                    "usage: shm_pingpong [-n iters] [-s bytes] "
                    "[-c cpu0,cpu1] [-p label] [-r run_id] [-H]\n");
            return 1;
        }
    }

    /* ── Create/open shared memory ────────────────────────────────────── */

    /* Two slots: ping buffer + pong buffer */
    size_t shm_size = 2 * msz;

    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, (off_t)shm_size) != 0) { perror("ftruncate"); return 1; }

    volatile char *shm = mmap(NULL, shm_size,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    /* ── Create named semaphores ──────────────────────────────────────── */

    sem_unlink(SEM_FWD); sem_unlink(SEM_BWD);

    sem_t *sem_fwd = sem_open(SEM_FWD, O_CREAT | O_EXCL, 0600, 0);
    sem_t *sem_bwd = sem_open(SEM_BWD, O_CREAT | O_EXCL, 0600, 0);
    if (sem_fwd == SEM_FAILED || sem_bwd == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    uint64_t *samples = alloc_samples(iters);

    /* Snapshot memory before benchmark (parent view) */
    mem_snapshot_t mem_before = mem_snapshot();

    /* ── Fork ─────────────────────────────────────────────────────────── */

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* Child = P2 (responder) */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_p2(shm, msz, sem_fwd, sem_bwd, iters);
        sem_close(sem_fwd);
        sem_close(sem_bwd);
        munmap((void *)shm, shm_size);
        exit(0);
    }

    /* Parent = P1 (initiator) */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    uint64_t t0 = now_ns();
    run_p1(shm, msz, sem_fwd, sem_bwd, iters, samples);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);

    /* Snapshot memory after benchmark */
    mem_snapshot_t mem_after = mem_snapshot();

    /* ── Compute and emit results ─────────────────────────────────────── */

    /* RTT → one-way latency */
    for (uint64_t i = 0; i < iters; i++) samples[i] /= 2;

    result_t r = {
        .samples      = samples,
        .n            = iters,
        .msg_size     = msz,
        .elapsed_ns   = elapsed,
        .mem_delta_kb = mem_delta_kb(&mem_before, &mem_after),
        .run_id       = run_id,
    };
    compute_stats(&r);

    /* Correct bidirectional throughput (same logic as mq_posix/pingpong.c) */
    double sec = (double)elapsed / 1e9;
    r.throughput_msg_s = (double)iters / sec;
    r.throughput_MB_s  = ((double)iters * 2.0 * (double)msz)
                          / sec / (1024.0 * 1024.0);

    print_csv_row_mech("shm_posix", "pingpong", 2, label, &r);

    /* ── Cleanup ──────────────────────────────────────────────────────── */

    sem_close(sem_fwd);  sem_unlink(SEM_FWD);
    sem_close(sem_bwd);  sem_unlink(SEM_BWD);
    munmap((void *)shm, shm_size);
    shm_unlink(SHM_NAME);
    free(samples);
    return 0;
}
