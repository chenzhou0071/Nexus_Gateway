#ifndef NEXUS_HTTP_PARSER_H
#define NEXUS_HTTP_PARSER_H

#include "nexus_http_header.h"

typedef enum {
    HP_REQ_LINE,
    HP_HEADER,
    HP_BODY,
    HP_DONE,
    HP_INVALID,
} hp_state_t;

typedef struct {
    hp_state_t       state;
    char             method[16];
    char             path[1024];
    char             version[16];
    nexus_headers_t  headers;
    int              body_remaining;
} nexus_http_req_t;

void nexus_http_req_init(nexus_http_req_t *r);
void nexus_http_req_reset(nexus_http_req_t *r);
int  nexus_http_req_feed(nexus_http_req_t *r, const char *data, size_t len);

#endif
