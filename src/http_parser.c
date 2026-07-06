#include "nexus_http_parser.h"
#include "nexus_util.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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
    strcpy(r->version, p);
    return 0;
}

int nexus_http_req_feed(nexus_http_req_t *r, const char *data, size_t len) {
    if (r->state == HP_DONE || r->state == HP_INVALID) return 0;
    size_t consumed = 0;

    while (consumed < len) {
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

        if (r->state == HP_REQ_LINE) {
            if (parse_request_line(r, line) != 0) { r->state = HP_INVALID; return (int)consumed; }
            r->state = HP_HEADER;
        } else if (r->state == HP_HEADER) {
            if (line_len == 0) {
                const char *cl = nexus_headers_get(&r->headers, "content-length");
                r->body_remaining = cl ? atoi(cl) : 0;
                r->state = (r->body_remaining > 0) ? HP_BODY : HP_DONE;
            } else {
                const char *colon = memchr(line, ':', line_len);
                if (!colon) { r->state = HP_INVALID; return (int)consumed; }
                size_t name_len = colon - line;
                const char *vp = colon + 1;
                size_t value_len = line_len - name_len - 1;
                while (value_len > 0 && (*vp == ' ' || *vp == '\t')) { vp++; value_len--; }
                nexus_headers_add(&r->headers, line, name_len, vp, value_len);
            }
        }
        consumed += line_len + 2;
    }
    return (int)consumed;
}
