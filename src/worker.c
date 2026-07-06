#define _GNU_SOURCE
#include "nexus_worker.h"
#include "nexus_epoll.h"
#include "nexus_acceptor.h"
#include "nexus_connection.h"
#include "nexus_http_parser.h"
#include "nexus_upstream.h"
#include "nexus_proxy.h"
#include "nexus_log.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static nexus_config_t *g_cfg = NULL;
static nexus_upstream_t g_upstream;
static int g_listen_fd = -1;

static int parse_listen_addr(const char *s, char *ip_out, int *port_out) {
    const char *colon = strrchr(s, ':');
    if (!colon) return -1;
    size_t iplen = colon - s;
    strncpy(ip_out, s, iplen); ip_out[iplen] = '\0';
    *port_out = atoi(colon + 1);
    return 0;
}

static void handle_client_read(int cfd) {
    nexus_conn_t *c = conn_get(cfd);
    char buf[4096];
    ssize_t n = read(cfd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        if (c) conn_free(c);
        close(cfd);
        return;
    }
    buf[n] = '\0';

    nexus_http_req_t req;
    nexus_http_req_init(&req);
    int consumed = nexus_http_req_feed(&req, buf, (size_t)n);
    if (req.state == HP_DONE) {
        const nexus_upstream_node_t *node = nexus_upstream_pick(&g_upstream);
        if (node) {
            int ufd = nexus_proxy_connect_upstream(node);
            if (ufd >= 0) {
                nexus_proxy_forward_request(ufd, buf, (size_t)n);
                char resp[8192];
                int rn = nexus_proxy_read_response(ufd, resp, sizeof(resp));
                if (rn > 0) write(cfd, resp, (size_t)rn);
                close(ufd);
            }
        }
        nexus_log_access("client_fd=%d %s %s -> %d", cfd, req.method, req.path, rn);
    }
    nexus_http_req_reset(&req);
    close(cfd);
    if (c) conn_free(c);
}

static void on_event(int fd, uint32_t ev, void *u) {
    (void)u;
    if (fd == g_listen_fd) {
        int cfd = nexus_acceptor_accept(g_listen_fd);
        if (cfd >= 0) {
            conn_alloc(cfd);
            nexus_epoll_add(cfd, EPOLLIN, NULL);
        }
    } else if (ev & EPOLLIN) {
        handle_client_read(fd);
    }
}

int nexus_worker_run(nexus_config_t *cfg) {
    g_cfg = cfg;
    nexus_log_init("logs", 1);

    const char *listen = nexus_config_get(cfg, "server", "listen");
    if (!listen) { fprintf(stderr, "missing [server] listen\n"); return 1; }
    char ip[64]; int port;
    if (parse_listen_addr(listen, ip, &port) < 0) return 1;

    // 初始化后端
    nexus_upstream_init(&g_upstream, "api");
    const char *srv = nexus_config_get(cfg, "upstream.api", "server");
    if (srv) {
        char host[64]; int sport;
        const char *colon = strrchr(srv, ':');
        if (colon) {
            size_t hl = colon - srv;
            memcpy(host, srv, hl); host[hl] = '\0';
            sport = atoi(colon + 1);
            nexus_upstream_add_node(&g_upstream, host, sport, 1);
        }
    }

    g_listen_fd = nexus_acceptor_listen(ip, port, 0);
    if (g_listen_fd < 0) { perror("listen"); return 1; }
    nexus_epoll_init();
    nexus_epoll_add(g_listen_fd, EPOLLIN, NULL);
    nexus_log_error(1, "worker started on %s:%d", ip, port);

    while (1) {
        nexus_epoll_wait(100, on_event);
    }
    return 0;
}
