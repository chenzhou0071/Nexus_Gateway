#ifndef NEXUS_MASTER_H
#define NEXUS_MASTER_H

#include "nexus_config.h"
#include <stdatomic.h>
#include <stdint.h>

#define CONFIG_MAX (64 * 1024)

typedef struct {
    uint64_t version;
    uint32_t total_size;
    char     data[CONFIG_MAX];
} config_shadow_t;

extern config_shadow_t *g_shadow;
extern atomic_int      *g_active_slot;
extern atomic_int      *g_shutting_down;
extern atomic_int      *g_drain_generation;

void nexus_shared_memory_init(void);

int nexus_master_run(nexus_config_t *cfg, const char *config_path);

#endif
