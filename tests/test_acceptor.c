#include "nexus_acceptor.h"
#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    int lfd = nexus_acceptor_listen("127.0.0.1", 19999, 1);
    assert(lfd >= 0);

    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19999);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    assert(connect(cfd, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    int accepted = nexus_acceptor_accept(lfd);
    assert(accepted >= 0);

    const char *msg = "ping";
    ssize_t wn = write(accepted, msg, 4);
    assert(wn == 4);
    char buf[16] = {0};
    ssize_t rn = read(cfd, buf, 4);
    assert(rn == 4);
    assert(strcmp(buf, "ping") == 0);

    close(accepted);
    close(cfd);
    close(lfd);

    // Task 4: 测试 accept 扩展
    int lfd2 = nexus_acceptor_listen("127.0.0.1", 19998, 1);
    assert(lfd2 >= 0);

    int cfd2 = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr2 = {0};
    addr2.sin_family = AF_INET;
    addr2.sin_port = htons(19998);
    inet_pton(AF_INET, "127.0.0.1", &addr2.sin_addr);
    assert(connect(cfd2, (struct sockaddr*)&addr2, sizeof(addr2)) == 0);

    client_addr_t client;
    int accepted2 = nexus_acceptor_accept_ext(lfd2, &client);
    assert(accepted2 >= 0);
    assert(strcmp(client.ip, "127.0.0.1") == 0);
    assert(client.port > 0);

    close(accepted2);
    close(cfd2);
    close(lfd2);
    printf("test_accept_ext: PASS\n");

    printf("test_acceptor: all passed\n");
    return 0;
}
