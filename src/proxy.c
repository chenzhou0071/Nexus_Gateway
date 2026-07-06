#include "nexus_proxy.h"
#include "nexus_util.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int nexus_proxy_connect_upstream(const nexus_upstream_node_t *node) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(node->port);
    inet_pton(AF_INET, node->host, &addr.sin_addr);
    int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (r < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    return fd;
}

int nexus_proxy_forward_request(int upstream_fd, const char *req, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(upstream_fd, req + sent, len - sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        sent += n;
    }
    return 0;
}

int nexus_proxy_read_response(int upstream_fd, char *buf, size_t cap) {
    ssize_t n = read(upstream_fd, buf, cap - 1);
    if (n < 0) return -1;
    buf[n] = '\0';
    return (int)n;
}
