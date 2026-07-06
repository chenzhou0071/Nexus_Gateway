#include "nexus_http_rewrite.h"
#include "nexus_http_header.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CUSTOM_HEADERS 32

// Base36 编码：毫秒时间戳 + 32 位随机数
char* generate_req_id(char *buf, size_t len) {
    uint64_t timestamp = nexus_now_ms();
    uint32_t random = (rand() << 16) | rand();
    uint64_t id = (timestamp << 32) | random;

    const char *chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *p = buf + len - 1;
    *p = '\0';
    p--;

    do {
        *p = chars[id % 36];
        id /= 36;
        p--;
    } while (id > 0 && p >= buf);

    return p + 1;
}

void nexus_http_rewrite_request(
    nexus_http_req_t *req,
    const char *client_ip,
    const char *gateway_version,
    nexus_config_t *cfg
) {
    if (!req) return;

    // 1. 兜底逻辑
    const char *real_ip = client_ip ? client_ip : "unknown";

    // 2. 清洗伪造头部（完全丢弃，不保留原有值）
    nexus_headers_remove(&req->headers, "X-Real-IP");
    nexus_headers_remove(&req->headers, "X-Forwarded-For");
    nexus_headers_remove(&req->headers, "X-Forwarded-Host");
    nexus_headers_remove(&req->headers, "X-Forwarded-Proto");

    // 3. 注入标准代理头部
    nexus_headers_set(&req->headers, "X-Real-IP", real_ip);
    nexus_headers_set(&req->headers, "X-Forwarded-For", real_ip);  // 只用真实 IP

    const char *host = nexus_headers_get(&req->headers, "Host");
    if (host) {
        nexus_headers_set(&req->headers, "X-Forwarded-Host", host);
    }

    nexus_headers_set(&req->headers, "X-Forwarded-Proto", "http");

    char via[64];
    snprintf(via, sizeof(via), "1.1 nexus-gateway/%s",
             gateway_version ? gateway_version : "1.0.0");
    nexus_headers_set(&req->headers, "Via", via);

    // 4. 生成 Req-ID
    char req_id[16];
    char *id = generate_req_id(req_id, sizeof(req_id));
    nexus_headers_set(&req->headers, "X-Gateway-Req-Id", id);

    // 5. 添加自定义头部（从配置实时读取）
    if (cfg) {
        char headers[MAX_CUSTOM_HEADERS][2][128];
        int count = nexus_config_get_custom_headers(cfg, headers, MAX_CUSTOM_HEADERS);
        for (int i = 0; i < count; i++) {
            nexus_headers_set(&req->headers, headers[i][0], headers[i][1]);
        }
    }
}
