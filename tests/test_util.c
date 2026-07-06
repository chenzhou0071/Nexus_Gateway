#include "nexus_util.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static void test_strdup(void) {
    char *s = nexus_strdup("hello");
    assert(strcmp(s, "hello") == 0);
    free(s);
}

static void test_strcmp_ci(void) {
    assert(nexus_strcmp_ci("Host", "host") == 0);
    assert(nexus_strcmp_ci("Content-Length", "content-length") == 0);
    assert(nexus_strcmp_ci("a", "b") != 0);
}

static void test_hash_fnv1a(void) {
    uint32_t a = nexus_hash_fnv1a("Host", 4);
    uint32_t b = nexus_hash_fnv1a("Host", 4);
    assert(a == b);
    uint32_t c = nexus_hash_fnv1a("host", 4);
    assert(a != c);
}

static void test_now_ms_monotonic(void) {
    uint64_t t1 = nexus_now_ms();
    for (volatile int i = 0; i < 1000000; i++);
    uint64_t t2 = nexus_now_ms();
    assert(t2 > t1);
}

int main(void) {
    test_strdup();
    test_strcmp_ci();
    test_hash_fnv1a();
    test_now_ms_monotonic();
    printf("test_util: all passed\n");
    return 0;
}
