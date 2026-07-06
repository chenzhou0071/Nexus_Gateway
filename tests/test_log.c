#include "nexus_log.h"
#include "nexus_util.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(void) {
    const char *dir = "/tmp/nexus_log_test";
    mkdir(dir, 0755);

    assert(nexus_log_init(dir, 1) == 0);

    nexus_log_access("127.0.0.1 GET / 200 %lu", nexus_now_ms());
    nexus_log_error(2, "test warning: %d", 42);

    nexus_log_close();

    struct stat st;
    assert(stat("/tmp/nexus_log_test/access.log", &st) == 0);
    assert(st.st_size > 0);
    assert(stat("/tmp/nexus_log_test/error.log", &st) == 0);
    assert(st.st_size > 0);

    printf("test_log: all passed\n");
    return 0;
}
