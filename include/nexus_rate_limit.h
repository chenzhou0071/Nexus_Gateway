#ifndef NEXUS_RATE_LIMIT_H
#define NEXUS_RATE_LIMIT_H

#include <stdint.h>

#define RL_MAX 1024

typedef struct {
    char     ip[64];
    double   tokens;
    uint64_t last_refill_ms;
} rl_entry_t;

typedef struct {
    rl_entry_t entries[RL_MAX];
    int        count;
    int        rate_per_sec;
} nexus_rate_limiter_t;

int nexus_rl_init(nexus_rate_limiter_t *rl, int rate_per_sec);
int nexus_rl_check(nexus_rate_limiter_t *rl, const char *ip);

#endif