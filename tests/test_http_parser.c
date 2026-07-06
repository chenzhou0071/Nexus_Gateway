#include "nexus_http_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *req1 =
        "GET /api HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    nexus_http_req_t r;
    nexus_http_req_init(&r);
    int n = nexus_http_req_feed(&r, req1, strlen(req1));
printf("nexus_http_req_feed returned %d\n", n);
printf("nexus_http_req_feed returned %d\n", n);
    assert(n > 0);
    assert(r.state == HP_DONE);
    assert(strcmp(r.method, "GET") == 0);
    assert(strcmp(r.path, "/api") == 0);
    assert(strcmp(nexus_headers_get(&r.headers, "host"), "example.com") == 0);
    assert(strcmp(nexus_headers_get(&r.headers, "content-length"), "5") == 0);
    nexus_http_req_reset(&r);

    const char *req2 =
        "POST /submit HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    nexus_http_req_init(&r);
    nexus_http_req_feed(&r, req2, strlen(req2));
    assert(r.state == HP_DONE);
    assert(strcmp(r.method, "POST") == 0);
    assert(strcmp(r.path, "/submit") == 0);
    nexus_http_req_reset(&r);

    const char *req3 = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    nexus_http_req_init(&r);
    nexus_http_req_feed(&r, req3, strlen(req3));
    assert(r.state == HP_DONE);
    nexus_http_req_reset(&r);

    const char *bad = "GET / HTTP/9.9\r\n\r\n";
    nexus_http_req_init(&r);
    nexus_http_req_feed(&r, bad, strlen(bad));
    assert(r.state == HP_INVALID);
    nexus_http_req_reset(&r);

    printf("test_http_parser: all passed\n");
    return 0;
}
