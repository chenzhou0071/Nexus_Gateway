#include "nexus_connection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    memset(g_conn_pool, 0, sizeof(g_conn_pool));

    nexus_conn_t *c1 = conn_alloc(7);
    assert(c1 && c1->fd == 7 && c1->state == CONN_IDLE);

    nexus_conn_t *c2 = conn_get(7);
    assert(c2 == c1);

    conn_free(c1);
    assert(conn_get(7) == NULL);

    printf("test_connection: all passed\n");
    return 0;
}
