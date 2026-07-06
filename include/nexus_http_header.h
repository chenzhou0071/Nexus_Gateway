#ifndef NEXUS_HTTP_HEADER_H
#define NEXUS_HTTP_HEADER_H

#include <stddef.h>

#define NEXUS_HTTP_HEADER_MAX 32

typedef struct {
    unsigned int hash;
    char *name;
    char *value;
} nexus_header_t;

typedef struct {
    nexus_header_t headers[NEXUS_HTTP_HEADER_MAX];
    int            count;
} nexus_headers_t;

void        nexus_headers_init(nexus_headers_t *h);
int         nexus_headers_add(nexus_headers_t *h,
                              const char *name, size_t name_len,
                              const char *value, size_t value_len);
const char *nexus_headers_get(const nexus_headers_t *h, const char *name);
void        nexus_headers_reset(nexus_headers_t *h);

#endif
