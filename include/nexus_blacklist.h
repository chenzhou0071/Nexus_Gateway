#ifndef NEXUS_BLACKLIST_H
#define NEXUS_BLACKLIST_H

#define BL_MAX 1024

typedef struct {
    char *ips[BL_MAX];
    int   count;
} nexus_blacklist_t;

int  nexus_bl_init(nexus_blacklist_t *bl);
int  nexus_bl_add(nexus_blacklist_t *bl, const char *ip);
int  nexus_bl_check(const nexus_blacklist_t *bl, const char *ip);
void nexus_bl_free(nexus_blacklist_t *bl);

#endif