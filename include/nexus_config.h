#ifndef NEXUS_CONFIG_H
#define NEXUS_CONFIG_H

#include <stddef.h>

#define NEXUS_VERSION "0.1.0"

typedef struct nexus_config_t nexus_config_t;

nexus_config_t *nexus_config_load(const char *path);
void            nexus_config_free(nexus_config_t *cfg);
const char     *nexus_config_get(nexus_config_t *cfg, const char *section, const char *key);
int             nexus_config_get_int(nexus_config_t *cfg, const char *section, const char *key, int def);

// 自定义头部访问函数（Task 5 新增）
int nexus_config_get_custom_headers(nexus_config_t *cfg, char headers[][2][128], int max_count);

#endif
