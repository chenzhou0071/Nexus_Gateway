#include "nexus_http_header.h"
#include "nexus_util.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>

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

static char *normalize_lower_strdup(const char *s) {
    size_t len = strlen(s);
    return normalize_lower(s, len);
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
    char *lower_name = normalize_lower_strdup(name);
    uint32_t target = nexus_hash_fnv1a(lower_name, strlen(lower_name));
    for (int i = 0; i < h->count; i++) {
        if (h->headers[i].hash == target) {
            free(lower_name);
            return h->headers[i].value;
        }
    }
    free(lower_name);
    return NULL;
}

int nexus_headers_set(nexus_headers_t *h, const char *name, const char *value) {
    if (!h || !name || !value) return -1;

    // 先删除同名头部（大小写不敏感）
    nexus_headers_remove(h, name);

    // 再添加新头部
    return nexus_headers_add(h, name, strlen(name), value, strlen(value));
}

void nexus_headers_remove(nexus_headers_t *h, const char *name) {
    if (!h || !name) return;

    char *lower_name = normalize_lower_strdup(name);
    uint32_t target = nexus_hash_fnv1a(lower_name, strlen(lower_name));

    int removed = 0;
    for (int i = 0; i < h->count; ) {
        // 先检查哈希，再检查字符串（避免哈希冲突误删）
        if (h->headers[i].hash == target &&
            strcmp(h->headers[i].name, lower_name) == 0) {
            free(h->headers[i].name);
            free(h->headers[i].value);

            // 后续头部前移
            for (int j = i; j < h->count - 1; j++) {
                h->headers[j] = h->headers[j + 1];
            }

            h->count--;
            removed++;
            // 不 i++，继续检查当前位置（前移后的新元素）
        } else {
            i++;  // 只有没删除时才前进
        }
    }

    free(lower_name);
}
