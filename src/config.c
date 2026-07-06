#define _POSIX_C_SOURCE 200809L
#include "nexus_config.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_KV 256

typedef struct {
    char *key;
    char *value;
} kv_t;

struct nexus_config_t {
    char *current_section;
    kv_t  kvs[MAX_KV];
    int   kv_count;
};

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end-1))) *--end = '\0';
    return s;
}

nexus_config_t *nexus_config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    nexus_config_t *cfg = nexus_xmalloc(sizeof(*cfg));
    memset(cfg, 0, sizeof(*cfg));
    cfg->current_section = nexus_strdup("default");

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#' || *p == ';') continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            free(cfg->current_section);
            cfg->current_section = nexus_strdup(p + 1);
        } else {
            char *eq = strchr(p, '=');
            if (!eq || cfg->kv_count >= MAX_KV) continue;
            *eq = '\0';
            char *k = trim(p);
            char *v = trim(eq + 1);
            char compound[256];
            snprintf(compound, sizeof(compound), "%s.%s", cfg->current_section, k);
            cfg->kvs[cfg->kv_count].key   = nexus_strdup(compound);
            cfg->kvs[cfg->kv_count].value = nexus_strdup(v);
            cfg->kv_count++;
        }
    }
    fclose(f);
    return cfg;
}

void nexus_config_free(nexus_config_t *cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->kv_count; i++) {
        free(cfg->kvs[i].key);
        free(cfg->kvs[i].value);
    }
    free(cfg->current_section);
    free(cfg);
}

const char *nexus_config_get(nexus_config_t *cfg, const char *section, const char *key) {
    char compound[256];
    snprintf(compound, sizeof(compound), "%s.%s", section, key);
    for (int i = 0; i < cfg->kv_count; i++) {
        if (strcmp(cfg->kvs[i].key, compound) == 0) {
            return cfg->kvs[i].value;
        }
    }
    return NULL;
}

int nexus_config_get_int(nexus_config_t *cfg, const char *section, const char *key, int def) {
    const char *v = nexus_config_get(cfg, section, key);
    if (!v) return def;
    return atoi(v);
}
