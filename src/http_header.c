#include "nexus_http_header.h"
#include "nexus_util.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>

void nexus_headers_init(nexus_headers_t *h) { memset(h, 0, sizeof(*h)); }

void nexus_headers_reset(nexus_headers_t *h) {
    for (int i = 0; i < h->count; i++) {
        free(h->headers[i].name);
        free(h->headers[i].value);
    }
    memset(h, 0, sizeof(*h));
}

static char *normalize_lower(const char *s, size_t len) {
    char *p = nexus_xmalloc(len + 1);
    for (size_t i = 0; i < len; i++) p[i] = (char)tolower((unsigned char)s[i]);
    p[len] = '\0';
    return p;
}

int nexus_headers_add(nexus_headers_t *h,
                      const char *name, size_t name_len,
                      const char *value, size_t value_len) {
    if (h->count >= NEXUS_HTTP_HEADER_MAX) return -1;
    char *n = normalize_lower(name, name_len);
    char *v = nexus_xmalloc(value_len + 1);
    memcpy(v, value, value_len);
    v[value_len] = '\0';
    nexus_header_t *e = &h->headers[h->count++];
    e->name  = n;
    e->value = v;
    e->hash  = nexus_hash_fnv1a(n, name_len);
    return 0;
}

const char *nexus_headers_get(const nexus_headers_t *h, const char *name) {
    size_t nlen = strlen(name);
    uint32_t target = nexus_hash_fnv1a(name, nlen);
    for (int i = 0; i < h->count; i++) {
        if (h->headers[i].hash == target && strcasecmp(h->headers[i].name, name) == 0) {
            return h->headers[i].value;
        }
    }
    return NULL;
}
