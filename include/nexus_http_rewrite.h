#ifndef NEXUS_HTTP_REWRITE_H
#define NEXUS_HTTP_REWRITE_H

#include "nexus_http_parser.h"
#include "nexus_config.h"

// HTTP 请求改写主函数
void nexus_http_rewrite_request(
    nexus_http_req_t *req,
    const char *client_ip,
    const char *gateway_version,
    nexus_config_t *cfg
);

// Req-ID 生成函数（测试用）
char* generate_req_id(char *buf, size_t len);

#endif
