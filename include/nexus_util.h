#ifndef NEXUS_UTIL_H
#define NEXUS_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

char    *nexus_strdup(const char *s);
int      nexus_strcmp_ci(const char *a, const char *b);
uint64_t nexus_now_ms(void);
uint64_t nexus_now_us(void);
uint32_t nexus_hash_fnv1a(const char *s, size_t len);
void    *nexus_xmalloc(size_t n);
void     nexus_die(const char *msg);

#endif
