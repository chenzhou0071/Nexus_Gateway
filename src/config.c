#define _POSIX_C_SOURCE 200809L
#include "nexus_config.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_KV 256
#define MAX_CUSTOM_HEADERS 32

typedef struct {
    char *key;
    char *value;
} kv_t;

struct nexus_config_t {
    char *current_section;
    kv_t  kvs[MAX_KV];
    int   kv_count;

    // 自定义头部（Task 5 新增）
    char custom_headers[MAX_CUSTOM_HEADERS][2][128];  // [index][0/1][len]
    int custom_header_count;
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

    // 解析自定义头部（Task 5 新增）
    cfg->custom_header_count = 0;
    const char *add_header_str = nexus_config_get(cfg, "http_rewrite", "add_header");
    if (add_header_str) {
        char *str = strdup(add_header_str);
        char *token = str;

        while (token && cfg->custom_header_count < MAX_CUSTOM_HEADERS) {
            char *comma = strchr(token, ',');
            if (comma) *comma = '\0';

            char *colon = strchr(token, ':');
            if (colon) {
                *colon = '\0';
                strncpy(cfg->custom_headers[cfg->custom_header_count][0],
                       token, 127);
                cfg->custom_headers[cfg->custom_header_count][0][127] = '\0';

                strncpy(cfg->custom_headers[cfg->custom_header_count][1],
                       colon + 1, 127);
                cfg->custom_headers[cfg->custom_header_count][1][127] = '\0';

                cfg->custom_header_count++;
            }

            token = comma ? comma + 1 : NULL;
        }

        free(str);
    }

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

int nexus_config_get_custom_headers(nexus_config_t *cfg, char headers[][2][128], int max_count) {
    if (!cfg || !headers) return 0;

    int count = cfg->custom_header_count < max_count ? cfg->custom_header_count : max_count;
    for (int i = 0; i < count; i++) {
        strncpy(headers[i][0], cfg->custom_headers[i][0], 127);
        headers[i][0][127] = '\0';
        strncpy(headers[i][1], cfg->custom_headers[i][1], 127);
        headers[i][1][127] = '\0';
    }

    return count;
}
