#ifndef NEXUS_ACCEPTOR_H
#define NEXUS_ACCEPTOR_H

#include <stddef.h>
#include <stdint.h>

int nexus_acceptor_listen(const char *bind_ip, int port, int reuse_port);
int nexus_acceptor_accept(int listen_fd);

// 客户端地址结构
typedef struct {
    char ip[64];
    uint16_t port;
} client_addr_t;

// 扩展 accept：返回客户端地址
int nexus_acceptor_accept_ext(int listen_fd, client_addr_t *client);

#endif
