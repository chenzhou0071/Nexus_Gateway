#include "nexus_http_header.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    nexus_headers_t h;
    nexus_headers_init(&h);

    assert(nexus_headers_add(&h, "Host", 4, "example.com", 11) == 0);
    assert(nexus_headers_add(&h, "Content-Length", 14, "42", 2) == 0);
    assert(nexus_headers_add(&h, "User-Agent", 10, "curl/7.0", 8) == 0);

    assert(strcmp(nexus_headers_get(&h, "host"), "example.com") == 0);
    assert(strcmp(nexus_headers_get(&h, "CONTENT-LENGTH"), "42") == 0);
    assert(strcmp(nexus_headers_get(&h, "user-agent"), "curl/7.0") == 0);
    assert(nexus_headers_get(&h, "missing") == NULL);

    for (int i = 0; i < 1000; i++) {
        nexus_headers_get(&h, "host");
    }

    nexus_headers_reset(&h);
    assert(h.count == 0);
    assert(nexus_headers_get(&h, "host") == NULL);

    printf("test_http_header: all passed\n");
    return 0;
}
