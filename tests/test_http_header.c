#include "nexus_http_header.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_headers_set() {
    nexus_headers_t h;
    nexus_headers_init(&h);

    // 添加第一个头部
    nexus_headers_add(&h, "X-Custom", 8, "value1", 6);
    assert(h.count == 1);
    assert(strcmp(h.headers[0].value, "value1") == 0);

    // 覆盖同名头部
    nexus_headers_set(&h, "X-Custom", "value2");
    assert(h.count == 1);  // 数量不变
    assert(strcmp(h.headers[0].value, "value2") == 0);  // 值更新

    nexus_headers_reset(&h);
    printf("test_headers_set: PASS\n");
}

void test_headers_remove() {
    nexus_headers_t h;
    nexus_headers_init(&h);

    nexus_headers_add(&h, "X-Real-IP", 9, "fake", 4);
    nexus_headers_add(&h, "Host", 4, "example.com", 11);
    nexus_headers_add(&h, "x-real-ip", 9, "another_fake", 12);  // 大小写不敏感

    assert(h.count == 3);

    // 删除 X-Real-IP（应删除所有同名头部）
    nexus_headers_remove(&h, "X-Real-IP");
    assert(h.count == 1);  // 只剩 Host
    assert(strcmp(h.headers[0].name, "host") == 0);

    nexus_headers_reset(&h);
    printf("test_headers_remove: PASS\n");
}

int main(void) {
    // 原有测试
    nexus_headers_t h;
    nexus_headers_init(&h);

    assert(nexus_headers_add(&h, "Host", 4, "example.com", 11) == 0);
    assert(nexus_headers_add(&h, "Content-Length", 14, "42", 2) == 0);
    assert(nexus_headers_add(&h, "User-Agent", 10, "curl/7.0", 8) == 0);

    assert(strcmp(nexus_headers_get(&h, "host"), "example.com") == 0);
    assert(strcmp(nexus_headers_get(&h, "CONTENT-LENGTH"), "42") == 0);
    assert(strcmp(nexus_headers_get(&h, "user-agent"), "curl/7.0") == 0);
    assert(nexus_headers_get(&h, "missing") == NULL);

    for (int i = 0; i < 1000; i++) {
        nexus_headers_get(&h, "host");
    }

    nexus_headers_reset(&h);
    assert(h.count == 0);
    assert(nexus_headers_get(&h, "host") == NULL);

    printf("test_http_header: original tests passed\n");

    // 新增测试
    test_headers_set();
    test_headers_remove();

    printf("test_http_header: all passed\n");
    return 0;
}
