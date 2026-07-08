#include "nexus_rate_limit.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

int main(void) {
    nexus_rate_limiter_t rl;
    nexus_rl_init(&rl, 10);  // 10 req/s

    // 前 10 个应通过
    for (int i = 0; i < 10; i++) {
        assert(nexus_rl_check(&rl, "1.2.3.4") == 1);
    }
    // 第 11 个被拒
    assert(nexus_rl_check(&rl, "1.2.3.4") == 0);
    // 不同 IP 互不影响
    assert(nexus_rl_check(&rl, "5.6.7.8") == 1);
    // 等待 1.1s 后同一 IP 应恢复
    struct timespec ts = {1, 100 * 1000 * 1000};
    nanosleep(&ts, NULL);
    assert(nexus_rl_check(&rl, "1.2.3.4") == 1);

    printf("test_rate_limit: all passed\n");
    return 0;
}