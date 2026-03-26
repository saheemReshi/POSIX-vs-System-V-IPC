#ifndef AFFINITY_H
#define AFFINITY_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>
#include <stdio.h>
#include <unistd.h>

static inline void pin_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        perror("sched_setaffinity (non-fatal)");
}

static inline int num_cpus(void)
{
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
}

#endif /* AFFINITY_H */
