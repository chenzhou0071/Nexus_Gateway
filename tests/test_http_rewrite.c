#include "nexus_http_rewrite.h"
#include "nexus_http_parser.h"
#include "nexus_config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

// 外部函数声明（从 http_rewrite.c）
extern char* generate_req_id(char *buf, size_t len);

void test_req_id_uniqueness() {
    char ids[10000][16];
    int dup = 0;

    for (int i = 0; i < 10000; i++) {
        char buf[16];
        char *id = generate_req_id(buf, sizeof(buf));
        strncpy(ids[i], id, 16);

        // 检查是否重复
        for (int j = 0; j < i; j++) {
            if (strcmp(ids[j], id) == 0) {
                dup++;
                break;
            }
        }
    }

    assert(dup == 0);
    printf("test_req_id_uniqueness: PASS (10000 IDs, %d duplicates)\n", dup);
}

void test_header_sanitization() {
    nexus_http_req_t req;
    nexus_http_req_init(&req);

    // 添加伪造头部
    nexus_headers_add(&req.headers, "X-Real-IP", 9, "fake_ip", 7);
    nexus_headers_add(&req.headers, "X-Forwarded-For", 15, "fake_proxy", 10);

    // 改写
    nexus_http_rewrite_request(&req, "192.168.1.100", "1.0.0", NULL);

    // 验证伪造头部被删除
    const char *real_ip = nexus_headers_get(&req.headers, "X-Real-IP");
    assert(strcmp(real_ip, "192.168.1.100") == 0);

    const char *xff = nexus_headers_get(&req.headers, "X-Forwarded-For");
    assert(strcmp(xff, "192.168.1.100") == 0);  // 不包含 fake_proxy

    nexus_http_req_reset(&req);
    printf("test_header_sanitization: PASS\n");
}

void test_xff_appending() {
    nexus_http_req_t req;
    nexus_http_req_init(&req);

    // 添加已有的 XFF（模拟多层代理）
    nexus_headers_add(&req.headers, "X-Forwarded-For", 15, "10.0.0.1, 10.0.0.2", 17);

    // 改写（现在会丢弃伪造的 XFF，只用真实 IP）
    nexus_http_rewrite_request(&req, "192.168.1.100", "1.0.0", NULL);

    // 验证：只有真实 IP（伪造的被丢弃）
    const char *xff = nexus_headers_get(&req.headers, "X-Forwarded-For");
    assert(strcmp(xff, "192.168.1.100") == 0);  // 不包含 10.0.0.1, 10.0.0.2

    nexus_http_req_reset(&req);
    printf("test_xff_appending: PASS\n");
}

int main() {
    test_req_id_uniqueness();
    test_header_sanitization();
    test_xff_appending();

    printf("\nAll http_rewrite tests passed!\n");
    return 0;
}
