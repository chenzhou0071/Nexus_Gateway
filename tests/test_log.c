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

    // Task 6: 测试完整日志格式（重新打开日志）
    nexus_log_init("/tmp/nexus_log_test", 1);
    nexus_log_access_full(
        "127.0.0.1",
        54321,
        "GET",
        "/",
        "HTTP/1.1",
        200,
        205,
        "curl/7.81.0",
        "3k5tm9hq",
        "127.0.0.1:19998",
        5.2
    );
    nexus_log_close();

    // 读取日志文件验证格式（读取最后一行）
    FILE *fp = fopen("/tmp/nexus_log_test/access.log", "r");
    assert(fp != NULL);

    char line[512];
    // 跳过前面的行，读最后一行
    while (fgets(line, sizeof(line) - 1, fp) != NULL) {
        // 继续读取直到文件结束
    }
    fclose(fp);

    // 验证关键字段
    assert(strstr(line, "127.0.0.1:54321") != NULL);
    assert(strstr(line, "\"GET / HTTP/1.1\"") != NULL);
    assert(strstr(line, "200 205") != NULL);
    assert(strstr(line, "req_id=3k5tm9hq") != NULL);
    assert(strstr(line, "duration=5.2ms") != NULL);

    printf("test_log_full: PASS\n");

    printf("test_log: all passed\n");
    return 0;
}
