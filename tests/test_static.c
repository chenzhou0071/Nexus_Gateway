#include "nexus_static.h"
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>

int main(void) {
    // 创建临时 root
    mkdir("/tmp/nexus_static_test", 0755);
    FILE *f = fopen("/tmp/nexus_static_test/hello.txt", "w");
    fputs("static file content\n", f); fclose(f);

    // 启动一个 TCP server on 19997 用 nexus_static_serve
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons(19997);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(srv, (struct sockaddr*)&addr, sizeof(addr));
    listen(srv, 1);

    pid_t p = fork();
    if (p == 0) {
        int c = accept(srv, NULL, NULL);
        nexus_static_serve(c, "/tmp/nexus_static_test", "/hello.txt");
        close(c);
        exit(0);
    }
    close(srv);
    sleep(0.2);

    // 客户端
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET; sa.sin_port = htons(19997);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    connect(c, (struct sockaddr*)&sa, sizeof(sa));

    char buf[4096] = {0};
    int n = read(c, buf, sizeof(buf) - 1);
    assert(n > 0);
    assert(strstr(buf, "200 OK") != NULL);
    assert(strstr(buf, "static file content") != NULL);

    close(c);
    waitpid(p, NULL, 0);
    unlink("/tmp/nexus_static_test/hello.txt");
    rmdir("/tmp/nexus_static_test");
    printf("test_static: all passed\n");
    return 0;
}