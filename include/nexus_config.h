#ifndef NEXUS_CONFIG_H
#define NEXUS_CONFIG_H

#include <stddef.h>

#define NEXUS_VERSION "0.1.0"

typedef struct nexus_config_t nexus_config_t;

nexus_config_t *nexus_config_load(const char *path);
void            nexus_config_free(nexus_config_t *cfg);
const char     *nexus_config_get(nexus_config_t *cfg, const char *section, const char *key);
int             nexus_config_get_int(nexus_config_t *cfg, const char *section, const char *key, int def);

#endif
