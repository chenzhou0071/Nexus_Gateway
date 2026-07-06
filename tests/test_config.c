#include "nexus_config.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static void write_tmp(const char *content) {
    FILE *f = fopen("/tmp/nexus_test.ini", "w");
    fputs(content, f); fclose(f);
}

int main(void) {
    write_tmp(
        "# 注释行\n"
        "[server]\n"
        "listen = 0.0.0.0:8080\n"
        "max_conns = 10000\n"
        "\n"
        "[upstream.api]\n"
        "server = 127.0.0.1:9001 weight=3\n"
        "; 也是注释\n"
        "[security]\n"
        "blacklist = 192.168.1.100,10.0.0.5\n"
    );
    nexus_config_t *cfg = nexus_config_load("/tmp/nexus_test.ini");
    assert(cfg);
    assert(strcmp(nexus_config_get(cfg, "server", "listen"), "0.0.0.0:8080") == 0);
    assert(nexus_config_get_int(cfg, "server", "max_conns", 0) == 10000);
    assert(strcmp(nexus_config_get(cfg, "upstream.api", "server"), "127.0.0.1:9001 weight=3") == 0);
    assert(nexus_config_get(cfg, "missing", "key") == NULL);
    assert(nexus_config_get_int(cfg, "missing", "key", 42) == 42);
    nexus_config_free(cfg);
    unlink("/tmp/nexus_test.ini");
    printf("test_config: all passed\n");
    return 0;
}
