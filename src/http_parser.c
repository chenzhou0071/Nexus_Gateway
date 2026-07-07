#include "nexus_http_parser.h"
#include "nexus_util.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

void nexus_http_req_init(nexus_http_req_t *r) {
    memset(r, 0, sizeof(*r));
    r->state = HP_REQ_LINE;
    nexus_headers_init(&r->headers);
}

void nexus_http_req_reset(nexus_http_req_t *r) {
    nexus_headers_reset(&r->headers);
    nexus_http_req_init(r);
}

static int parse_request_line(nexus_http_req_t *r, const char *line) {
    const char *p = line;
    const char *sp1 = strchr(p, ' ');
    if (!sp1 || sp1 - p >= (int)sizeof(r->method)) return -1;
    memcpy(r->method, p, sp1 - p);
    r->method[sp1 - p] = '\0';

    p = sp1 + 1;
    const char *sp2 = strchr(p, ' ');
    if (!sp2 || sp2 - p >= (int)sizeof(r->path)) return -1;
    memcpy(r->path, p, sp2 - p);
    r->path[sp2 - p] = '\0';

    p = sp2 + 1;
    if (strncmp(p, "HTTP/", 5) != 0) return -1;
    if (strlen(p) >= sizeof(r->version)) return -1;
    if (strcmp(p, "HTTP/1.0") != 0 && strcmp(p, "HTTP/1.1") != 0) return -1;
    strcpy(r->version, p);
    return 0;
}

int nexus_http_req_feed(nexus_http_req_t *r, const char *data, size_t len) {
    if (r->state == HP_DONE || r->state == HP_INVALID) return 0;
    size_t consumed = 0;

    if (len == 0) return 0;



    while (consumed < len && r->state != HP_DONE && r->state != HP_INVALID) {
        if (r->state == HP_BODY) {
            size_t take = (len - consumed) < (size_t)r->body_remaining
                          ? (len - consumed) : (size_t)r->body_remaining;
            consumed += take;
            r->body_remaining -= (int)take;
            if (r->body_remaining == 0) r->state = HP_DONE;
            continue;
        }

        const char *eol = NULL;
        for (size_t i = consumed; i + 1 < len; i++) {
            if (data[i] == '\r' && data[i+1] == '\n') { eol = data + i; break; }
        }
        if (!eol) return (int)consumed;

        size_t line_len = eol - (data + consumed);
        const char *line = data + consumed;

        // 临时复制一行，确保 null-terminated
        char line_buf[1024];
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, line, line_len);
        line_buf[line_len] = '\0';

        if (r->state == HP_REQ_LINE) {
            if (parse_request_line(r, line_buf) != 0) { r->state = HP_INVALID; return (int)consumed; }
            r->state = HP_HEADER;
        } else if (r->state == HP_HEADER) {
            if (line_len == 0) {
                const char *cl = nexus_headers_get(&r->headers, "content-length");
                r->body_remaining = cl ? atoi(cl) : 0;
                r->state = (r->body_remaining > 0) ? HP_BODY : HP_DONE;
            } else {
                const char *colon = memchr(line_buf, ':', line_len);
                if (!colon) { r->state = HP_INVALID; return (int)consumed; }
                size_t name_len = colon - line_buf;
                const char *vp = colon + 1;
                size_t value_len = line_len - name_len - 1;
                while (value_len > 0 && (*vp == ' ' || *vp == '\t')) { vp++; value_len--; }

                // 防御性检查：头部数量限制（Task 3 新增）
                if (r->headers.count >= 100) {
                    r->state = HP_INVALID;  // 标记为非法
                    return (int)consumed;
                }

                if (nexus_headers_add(&r->headers, line_buf, name_len, vp, value_len) != 0) {
                    r->state = HP_INVALID;  // 添加失败（超过 MAX）也标记为非法
                    return (int)consumed;
                }
            }
        }
        consumed += line_len + 2;
    }
    return (int)consumed;
}

// 序列化 HTTP 请求（用于改写后发送给上游）
int nexus_http_req_serialize(const nexus_http_req_t *r, char *out, size_t out_len) {
    if (!r || !out || out_len == 0) return -1;

    // 计算所需长度：请求行 + 所有头部 + "\r\n\r\n"
    size_t needed = snprintf(NULL, 0, "%s %s %s\r\n", r->method, r->path, r->version);
    for (int i = 0; i < r->headers.count; i++) {
        const nexus_header_t *h = &r->headers.headers[i];
        if (h->name[0] != '\0') {
            needed += snprintf(NULL, 0, "%s: %s\r\n", h->name, h->value);
        }
    }
    needed += 2;  // "\r\n"

    if (needed >= out_len) return -1;  // 缓冲区不够

    // 序列化请求行
    size_t pos = snprintf(out, out_len, "%s %s %s\r\n", r->method, r->path, r->version);

    // 序列化所有头部
    for (int i = 0; i < r->headers.count; i++) {
        const nexus_header_t *h = &r->headers.headers[i];
        if (h->name[0] != '\0') {
            pos += snprintf(out + pos, out_len - pos, "%s: %s\r\n", h->name, h->value);
        }
    }

    // 结束符
    pos += snprintf(out + pos, out_len - pos, "\r\n");

    return (int)pos;
}

