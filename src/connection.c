#include "nexus_connection.h"
#include "nexus_util.h"
#include <string.h>
#include <stddef.h>

nexus_conn_t g_conn_pool[CONN_POOL_SIZE];

nexus_conn_t *conn_alloc(int fd) {
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (g_conn_pool[i].fd == 0) {
            g_conn_pool[i].fd = fd;
            g_conn_pool[i].state = CONN_IDLE;
            g_conn_pool[i].created_ms = nexus_now_ms();
            g_conn_pool[i].last_active_ms = g_conn_pool[i].created_ms;
            return &g_conn_pool[i];
        }
    }
    return NULL;
}

void conn_free(nexus_conn_t *c) {
    if (!c) return;
    c->fd = 0;
    c->state = CONN_IDLE;
}

nexus_conn_t *conn_get(int fd) {
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (g_conn_pool[i].fd == fd) return &g_conn_pool[i];
    }
    return NULL;
}
