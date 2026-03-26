#ifndef STATS_H
#define STATS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

static inline uint64_t *alloc_samples(uint64_t n)
{
    uint64_t *s = malloc(n * sizeof(uint64_t));
    if (!s) { perror("malloc samples"); exit(1); }
    return s;
}

#endif /* STATS_H */
