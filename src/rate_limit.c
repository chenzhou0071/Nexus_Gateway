#include "nexus_rate_limit.h"
#include "nexus_util.h"
#include <string.h>
#include <stddef.h>

int nexus_rl_init(nexus_rate_limiter_t *rl, int rate_per_sec) {
    memset(rl, 0, sizeof(*rl));
    rl->rate_per_sec = rate_per_sec;
    return 0;
}

static rl_entry_t *find_or_create(nexus_rate_limiter_t *rl, const char *ip) {
    for (int i = 0; i < rl->count; i++) {
        if (strcmp(rl->entries[i].ip, ip) == 0) return &rl->entries[i];
    }
    if (rl->count >= RL_MAX) return NULL;
    rl_entry_t *e = &rl->entries[rl->count++];
    strncpy(e->ip, ip, sizeof(e->ip) - 1);
    e->tokens = (double)rl->rate_per_sec;
    e->last_refill_ms = nexus_now_ms();
    return e;
}

int nexus_rl_check(nexus_rate_limiter_t *rl, const char *ip) {
    rl_entry_t *e = find_or_create(rl, ip);
    if (!e) return 1;  // 满了放行（fail-open）
    uint64_t now = nexus_now_ms();
    double elapsed = (now - e->last_refill_ms) / 1000.0;
    e->tokens = e->tokens + elapsed * rl->rate_per_sec;
    if (e->tokens > rl->rate_per_sec) e->tokens = (double)rl->rate_per_sec;
    e->last_refill_ms = now;
    if (e->tokens >= 1.0) {
        e->tokens -= 1.0;
        return 1;
    }
    return 0;
}