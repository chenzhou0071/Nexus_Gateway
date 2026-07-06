#include "nexus_acceptor.h"
#include "nexus_util.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int nexus_acceptor_listen(const char *bind_ip, int port, int reuse_port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (reuse_port) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (strcmp(bind_ip, "0.0.0.0") == 0) addr.sin_addr.s_addr = INADDR_ANY;
    else inet_pton(AF_INET, bind_ip, &addr.sin_addr);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    if (listen(fd, 1024) < 0) {
        close(fd); return -1;
    }
    set_nonblock(fd);
    return fd;
}

int nexus_acceptor_accept(int listen_fd) {
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int cfd = accept4(listen_fd, (struct sockaddr*)&cli, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    return cfd;
}
