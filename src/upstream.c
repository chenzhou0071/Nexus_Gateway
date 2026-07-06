#include "nexus_upstream.h"
#include <string.h>
#include <stddef.h>

static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }

int nexus_upstream_init(nexus_upstream_t *u, const char *name) {
    memset(u, 0, sizeof(*u));
    strncpy(u->name, name, sizeof(u->name) - 1);
    return 0;
}

int nexus_upstream_add_node(nexus_upstream_t *u, const char *host, int port, int weight) {
    if (u->node_count >= NEXUS_UPSTREAM_MAX_NODES) return -1;
    nexus_upstream_node_t *n = &u->nodes[u->node_count++];
    strncpy(n->host, host, sizeof(n->host) - 1);
    n->port = port;
    n->weight = weight > 0 ? weight : 1;
    n->healthy = 1;
    return 0;
}

static void recompute(nexus_upstream_t *u) {
    int mw = 0, g = 0;
    for (int i = 0; i < u->node_count; i++) {
        if (u->nodes[i].weight > mw) mw = u->nodes[i].weight;
        if (i == 0) g = u->nodes[i].weight;
        else g = gcd(g, u->nodes[i].weight);
    }
    u->max_weight = mw;
    u->gcd_weight = g > 0 ? g : 1;
}

const nexus_upstream_node_t *nexus_upstream_pick(nexus_upstream_t *u) {
    if (u->node_count == 0) return NULL;
    if (u->max_weight == 0) recompute(u);

    int total = 0;
    for (int i = 0; i < u->node_count; i++) {
        if (!u->nodes[i].healthy) continue;
        u->nodes[i].active_conns += u->nodes[i].weight;
        total += u->nodes[i].weight;
    }
    if (total == 0) return NULL;

    int best = -1, best_val = -1;
    for (int i = 0; i < u->node_count; i++) {
        if (!u->nodes[i].healthy) continue;
        if (best == -1 || u->nodes[i].active_conns > best_val) {
            best = i;
            best_val = u->nodes[i].active_conns;
        }
    }
    if (best < 0) return NULL;
    u->nodes[best].active_conns -= total;
    return &u->nodes[best];
}

void nexus_upstream_mark(nexus_upstream_t *u, const char *host, int port, int healthy) {
    for (int i = 0; i < u->node_count; i++) {
        if (u->nodes[i].port == port && strcmp(u->nodes[i].host, host) == 0) {
            u->nodes[i].healthy = healthy;
        }
    }
}
