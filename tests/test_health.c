#include "nexus_health.h"
#include "nexus_upstream.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    nexus_upstream_t u;
    nexus_upstream_init(&u, "test");
    // 加一个不可达的上游
    nexus_upstream_add_node(&u, "127.0.0.1", 1, 1);  // port 1 几乎肯定没人

    nexus_health_checker_t *h = nexus_health_create(&u, 1, 2);
    nexus_health_start(h);

    sleep(3);  // 等 2 次心跳

    // 不健康
    assert(u.nodes[0].healthy == 0);
    nexus_health_stop(h);
    nexus_health_destroy(h);
    printf("test_health: all passed\n");
    return 0;
}