#include "nexus_blacklist.h"
#include "nexus_util.h"
#include <string.h>
#include <stdlib.h>

int nexus_bl_init(nexus_blacklist_t *bl) {
    memset(bl, 0, sizeof(*bl));
    return 0;
}

int nexus_bl_add(nexus_blacklist_t *bl, const char *ip) {
    if (bl->count >= BL_MAX) return -1;
    bl->ips[bl->count++] = nexus_strdup(ip);
    return 0;
}

int nexus_bl_check(const nexus_blacklist_t *bl, const char *ip) {
    for (int i = 0; i < bl->count; i++) {
        if (strcmp(bl->ips[i], ip) == 0) return 1;
    }
    return 0;
}

void nexus_bl_free(nexus_blacklist_t *bl) {
    for (int i = 0; i < bl->count; i++) free(bl->ips[i]);
    memset(bl, 0, sizeof(*bl));
}