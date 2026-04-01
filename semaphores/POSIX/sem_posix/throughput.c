/*
 * throughput.c — Producer→Consumer Throughput via POSIX Semaphores
 *
 * Two semaphores implement a bounded signalling channel:
 *
 *   sem_slots  (init = depth)  — free slots, producer waits when full
 *   sem_items  (init = 0)      — pending items, consumer waits when empty
 *
 *   Producer:   sem_wait(slots)  →  sem_post(items)   × N
 *   Consumer:   sem_wait(items)  →  sem_post(slots)   × N
 *
 * This is the classic pure-semaphore bounded producer-consumer.
 * The depth parameter is directly analogous to the MQ queue depth:
 * it controls how many ops the producer can run ahead of the consumer
 * before blocking, making this benchmark structurally identical to
 * its POSIX MQ counterpart.
 *
 * Elapsed time is measured around the producer loop only
 * (producer-side throughput), matching the MQ convention.
 *
 * msg_size is always 0 and throughput_MB_s is always 0.
 * throughput_msg_s reports synchronisation ops per second.
 *
 * Build note: pass -DMECHANISM='"sem_posix"' when compiling.
 *
 * Usage: throughput [-n ops] [-q depth] [-c cpu0,cpu1] [-p label] [-H]
 */

#ifndef MECHANISM
#define MECHANISM "sem_posix"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <stdint.h>

#include "../common/timing.h"
#include "../common/affinity.h"

#define SEM_SLOTS "/ipc_sem_tput_slots"
#define SEM_ITEMS "/ipc_sem_tput_items"

/* Producer: drain a slot, produce an item — N times */
static void run_producer(sem_t *slots, sem_t *items, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        if (sem_wait(slots) != 0) { perror("sem_wait slots"); exit(1); }
        if (sem_post(items) != 0) { perror("sem_post items"); exit(1); }
    }
}

/* Consumer: consume an item, free a slot — N times */
static void run_consumer(sem_t *slots, sem_t *items, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        if (sem_wait(items) != 0) { perror("sem_wait items"); exit(1); }
        if (sem_post(slots) != 0) { perror("sem_post slots"); exit(1); }
    }
}

int main(int argc, char *argv[])
{
    uint64_t    n_ops  = 100000;
    unsigned    depth  = 64;      /* initial value of sem_slots */
    int         cpu0   = -1, cpu1 = -1;
    const char *label  = "unspecified";
    int         opt;

    while ((opt = getopt(argc, argv, "n:q:c:p:H")) != -1) {
        switch (opt) {
        case 'n': n_ops = strtoull(optarg, NULL, 10);      break;
        case 'q': depth = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'c': sscanf(optarg, "%d,%d", &cpu0, &cpu1);  break;
        case 'p': label = optarg;                          break;
        case 'H': print_csv_header(); return 0;
        default:
            fprintf(stderr,
                    "usage: throughput [-n ops] [-q depth] "
                    "[-c cpu0,cpu1] [-p label] [-H]\n");
            return 1;
        }
    }

    /* Tear down any stale semaphores */
    sem_unlink(SEM_SLOTS);
    sem_unlink(SEM_ITEMS);

    /* sem_slots starts at depth:  producer can run that many ops
     *   ahead before blocking, exactly like an MQ with depth slots.
     * sem_items starts at 0:      consumer blocks until the first
     *   producer post arrives.
     * Both opened before fork() to prevent startup races.           */
    sem_t *slots = sem_open(SEM_SLOTS, O_CREAT | O_EXCL, 0600, depth);
    sem_t *items = sem_open(SEM_ITEMS, O_CREAT | O_EXCL, 0600, 0);
    if (slots == SEM_FAILED || items == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        /* Child is the consumer */
        if (cpu1 >= 0) pin_to_cpu(cpu1);
        run_consumer(slots, items, n_ops);
        sem_close(slots);
        sem_close(items);
        exit(0);
    }

    /* Parent is the producer */
    if (cpu0 >= 0) pin_to_cpu(cpu0);

    uint64_t t0 = now_ns();
    run_producer(slots, items, n_ops);
    uint64_t elapsed = now_ns() - t0;

    waitpid(pid, NULL, 0);

    double sec = elapsed / 1e9;
    result_t r = {
        .n                = n_ops,
        .msg_size         = 0,          /* no payload */
        .elapsed_ns       = elapsed,
        .avg_ns           = (double)elapsed / n_ops,
        .throughput_msg_s = n_ops / sec, /* synchronisation ops/sec */
        .throughput_MB_s  = 0.0,         /* no data transferred */
    };
    print_csv_row("throughput", 2, label, &r);

    sem_close(slots);
    sem_close(items);
    sem_unlink(SEM_SLOTS);
    sem_unlink(SEM_ITEMS);
    return 0;
}
