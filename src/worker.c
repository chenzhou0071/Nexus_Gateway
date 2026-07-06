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
#include <errno.h>

static nexus_config_t *g_cfg = NULL;
static nexus_upstream_t g_upstream;
static int g_listen_fd = -1;

typedef struct {
    int          fd;
    conn_state_t state;
    int          upstream_fd;      // -1 if client
    char         req_buf[4096];   // 请求缓冲
    size_t       req_len;
    char         resp_buf[8192];  // 响应缓冲
    size_t       resp_len;
} proxy_conn_t;

#define PROXY_POOL_SIZE 1024
static proxy_conn_t g_proxy_pool[PROXY_POOL_SIZE];

static proxy_conn_t *proxy_alloc(int fd) {
    for (int i = 0; i < PROXY_POOL_SIZE; i++) {
        if (g_proxy_pool[i].fd == 0) {
            g_proxy_pool[i].fd = fd;
            g_proxy_pool[i].state = CONN_IDLE;
            g_proxy_pool[i].upstream_fd = -1;
            return &g_proxy_pool[i];
        }
    }
    return NULL;
}

static void proxy_free(proxy_conn_t *c) {
    if (!c) return;
    c->fd = 0;
    c->upstream_fd = -1;
    c->req_len = 0;
    c->resp_len = 0;
}

static proxy_conn_t *proxy_get(int fd) {
    for (int i = 0; i < PROXY_POOL_SIZE; i++) {
        if (g_proxy_pool[i].fd == fd || g_proxy_pool[i].upstream_fd == fd) {
            return &g_proxy_pool[i];
        }
    }
    return NULL;
}

static int parse_listen_addr(const char *s, char *ip_out, int *port_out) {
    const char *colon = strrchr(s, ':');
    if (!colon) return -1;
    size_t iplen = colon - s;
    strncpy(ip_out, s, iplen); ip_out[iplen] = '\0';
    *port_out = atoi(colon + 1);
    return 0;
}

static void handle_client_read(proxy_conn_t *c) {
    ssize_t n = read(c->fd, c->req_buf, sizeof(c->req_buf) - 1);
    if (n <= 0) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }
    c->req_buf[n] = '\0';
    c->req_len = (size_t)n;

    nexus_http_req_t req;
    nexus_http_req_init(&req);
    nexus_http_req_feed(&req, c->req_buf, c->req_len);
    if (req.state != HP_DONE) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }

    const nexus_upstream_node_t *node = nexus_upstream_pick(&g_upstream);
    if (!node) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }

    int ufd = nexus_proxy_connect_upstream(node);
    if (ufd < 0) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }

    c->upstream_fd = ufd;
    c->state = CONN_CONNECTING_UPSTREAM;

    nexus_epoll_add(ufd, EPOLLOUT | EPOLLIN, c);
    nexus_epoll_mod(c->fd, EPOLLIN, c);
}

static void handle_upstream_writable(proxy_conn_t *c) {
    int ufd = c->upstream_fd;
    if (ufd < 0) return;

    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(ufd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err) {
        close(ufd);
        nexus_epoll_del(ufd);
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }

    ssize_t n = write(ufd, c->req_buf, c->req_len);
    if (n > 0) {
        c->state = CONN_READING_UPSTREAM;
        nexus_epoll_mod(ufd, EPOLLIN, c);
    }
}

static void handle_upstream_readable(proxy_conn_t *c) {
    int ufd = c->upstream_fd;
    if (ufd < 0) return;

    ssize_t n = read(ufd, c->resp_buf, sizeof(c->resp_buf) - 1);
    if (n > 0) {
        c->resp_buf[n] = '\0';
        c->resp_len = (size_t)n;

        // 转发给客户端
        ssize_t wn = write(c->fd, c->resp_buf, c->resp_len);
        nexus_log_access("proxied %zd bytes (read %zd, wrote %zd)", c->resp_len, n, wn);

        // 关闭所有连接
        close(ufd);
        nexus_epoll_del(ufd);
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
    } else if (n == 0) {
        // 上游关闭连接
        close(ufd);
        nexus_epoll_del(ufd);
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
    } else {
        // n < 0，检查错误
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 数据还没到，继续等
            return;
        } else {
            // 真正的错误
            close(ufd);
            nexus_epoll_del(ufd);
            close(c->fd);
            nexus_epoll_del(c->fd);
            proxy_free(c);
        }
    }
}

static void on_event(int fd, uint32_t ev, void *user) {
    (void)user;
    if (fd == g_listen_fd) {
        int cfd = nexus_acceptor_accept(g_listen_fd);
        if (cfd >= 0) {
            proxy_conn_t *c = proxy_alloc(cfd);
            if (c) {
                c->state = CONN_READING_REQ;
                nexus_epoll_add(cfd, EPOLLIN, c);
            }
        }
    } else {
        proxy_conn_t *c = proxy_get(fd);
        if (!c) return;

        if (c->state == CONN_READING_REQ && (ev & EPOLLIN)) {
            handle_client_read(c);
        } else if (c->state == CONN_CONNECTING_UPSTREAM && ((ev & EPOLLOUT) || (ev & EPOLLIN))) {
            handle_upstream_writable(c);
        } else if (c->state == CONN_READING_UPSTREAM && (ev & EPOLLIN)) {
            handle_upstream_readable(c);
        }
    }
}

int nexus_worker_run(nexus_config_t *cfg) {
    g_cfg = cfg;
    nexus_log_init("logs", 1);

    // 初始化连接池
    memset(g_proxy_pool, 0, sizeof(g_proxy_pool));

    const char *listen = nexus_config_get(cfg, "server", "listen");
    if (!listen) { fprintf(stderr, "missing [server] listen\n"); return 1; }
    char ip[64]; int port;
    if (parse_listen_addr(listen, ip, &port) < 0) return 1;

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
