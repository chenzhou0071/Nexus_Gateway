#ifndef NEXUS_UPSTREAM_H
#define NEXUS_UPSTREAM_H

#define NEXUS_UPSTREAM_MAX_NODES 16

typedef struct {
    char host[64];
    int  port;
    int  weight;
    int  healthy;
    int  active_conns;
} nexus_upstream_node_t;

typedef struct {
    char                  name[64];
    nexus_upstream_node_t nodes[NEXUS_UPSTREAM_MAX_NODES];
    int                   node_count;
    int                   cur_weight;
    int                   max_weight;
    int                   gcd_weight;
} nexus_upstream_t;

int  nexus_upstream_init(nexus_upstream_t *u, const char *name);
int  nexus_upstream_add_node(nexus_upstream_t *u, const char *host, int port, int weight);
const nexus_upstream_node_t *nexus_upstream_pick(nexus_upstream_t *u);
void nexus_upstream_mark(nexus_upstream_t *u, const char *host, int port, int healthy);

#endif
