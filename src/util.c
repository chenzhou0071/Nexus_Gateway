#define _POSIX_C_SOURCE 200809L
#include "nexus_util.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint64_t mono_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

uint64_t nexus_now_ms(void) { return mono_ms(); }
uint64_t nexus_now_us(void) { return mono_us(); }

char *nexus_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = nexus_xmalloc(n);
    memcpy(p, s, n);
    return p;
}

int nexus_strcmp_ci(const char *a, const char *b) {
    return strcasecmp(a, b);
}

uint32_t nexus_hash_fnv1a(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

void *nexus_xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) nexus_die("xmalloc: out of memory");
    return p;
}

void nexus_die(const char *msg) {
    fprintf(stderr, "nexus: fatal: %s\n", msg);
    abort();
}
