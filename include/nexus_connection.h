#ifndef NEXUS_CONNECTION_H
#define NEXUS_CONNECTION_H

#include <stdint.h>

#define CONN_POOL_SIZE 1024

typedef enum {
    CONN_IDLE,
    CONN_READING_REQ,
    CONN_CONNECTING_UPSTREAM,
    CONN_WRITING_UPSTREAM,
    CONN_READING_UPSTREAM,
    CONN_WRITING_CLIENT,
    CONN_CLOSING,
} conn_state_t;

typedef struct {
    int          fd;
    conn_state_t state;
    uint64_t     created_ms;
    uint64_t     last_active_ms;
} nexus_conn_t;

extern nexus_conn_t g_conn_pool[CONN_POOL_SIZE];

nexus_conn_t *conn_alloc(int fd);
void          conn_free(nexus_conn_t *c);
nexus_conn_t *conn_get(int fd);

#endif
