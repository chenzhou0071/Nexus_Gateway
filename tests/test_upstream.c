#include "nexus_upstream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    nexus_upstream_t u;
    nexus_upstream_init(&u, "api");
    nexus_upstream_add_node(&u, "127.0.0.1", 9001, 3);
    nexus_upstream_add_node(&u, "127.0.0.1", 9002, 2);
    nexus_upstream_add_node(&u, "127.0.0.1", 9003, 1);

    int hits[3] = {0,0,0};
    for (int i = 0; i < 6000; i++) {
        const nexus_upstream_node_t *n = nexus_upstream_pick(&u);
        for (int j = 0; j < 3; j++) {
            if (n->port == 9001 + j) hits[j]++;
        }
    }
    assert(hits[0] > hits[1] && hits[1] > hits[2]);
    assert(hits[0] > 2700 && hits[0] < 3300);

    nexus_upstream_mark(&u, "127.0.0.1", 9002, 0);
    int saw_9002 = 0;
    for (int i = 0; i < 100; i++) {
        if (nexus_upstream_pick(&u)->port == 9002) saw_9002++;
    }
    assert(saw_9002 == 0);

    printf("test_upstream: hits=%d/%d/%d (want ~3000/2000/1000)\n", hits[0], hits[1], hits[2]);
    printf("test_upstream: all passed\n");
    return 0;
}
