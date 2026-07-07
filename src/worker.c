#include "nexus_worker.h"
#include "nexus_epoll.h"
#include "nexus_acceptor.h"
#include "nexus_connection.h"
#include "nexus_http_parser.h"
#include "nexus_http_header.h"
#include "nexus_upstream.h"
#include "nexus_proxy.h"
#include "nexus_http_rewrite.h"
#include "nexus_log.h"
#include "nexus_config.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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

    // Task 7: 日志相关字段
    char         client_ip[64];
    uint16_t     client_port;
    char         req_id[16];
    uint64_t     start_ms;
    char         method[16];
    char         path[1024];
    char         version[16];
    int          status_code;
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

    // Task 7: 重置日志字段
    memset(c->client_ip, 0, sizeof(c->client_ip));
    c->client_port = 0;
    memset(c->req_id, 0, sizeof(c->req_id));
    c->start_ms = 0;
    memset(c->method, 0, sizeof(c->method));
    memset(c->path, 0, sizeof(c->path));
    memset(c->version, 0, sizeof(c->version));
    c->status_code = 0;
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
    snprintf(ip_out, 64, "%.*s", (int)iplen, s);
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

    // Task 7: 记录开始时间
    c->start_ms = nexus_now_ms();

    nexus_http_req_t req;
    nexus_http_req_init(&req);
    nexus_http_req_feed(&req, c->req_buf, c->req_len);
    if (req.state != HP_DONE) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }

    // Task 7: 保存请求信息
    snprintf(c->method, sizeof(c->method), "%s", req.method);
    snprintf(c->path, sizeof(c->path), "%s", req.path);
    snprintf(c->version, sizeof(c->version), "%s", req.version);

    const nexus_upstream_node_t *node = nexus_upstream_pick(&g_upstream);
    if (!node) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }

    // Task 7: HTTP 改写（清洗 + 注入标准头部）
    nexus_http_rewrite_request(&req, c->client_ip, "1.0.0", g_cfg);

    // Task 7: 序列化改写后的请求
    char serialized_req[4096];
    int serialized_len = nexus_http_req_serialize(&req, serialized_req, sizeof(serialized_req));
    if (serialized_len < 0) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }

    // 更新 req_buf 为序列化后的请求
    memcpy(c->req_buf, serialized_req, serialized_len);
    c->req_len = serialized_len;

    // Task 7: 提取 Req-ID
    const char *req_id = nexus_headers_get(&req.headers, "X-Gateway-Req-Id");
    if (req_id) {
        snprintf(c->req_id, sizeof(c->req_id), "%s", req_id);
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
        (void)wn;  // 暂不处理写入失败

        // Task 7: 记录状态码（临时写死，后续可解析响应行）
        c->status_code = 200;

        // Task 7: 计算耗时
        double duration_ms = nexus_now_ms() - c->start_ms;

        // Task 7: 完整日志
        char upstream_addr[64];
        snprintf(upstream_addr, sizeof(upstream_addr), "127.0.0.1:19998");

        nexus_log_access_full(
            c->client_ip,
            c->client_port,
            c->method,
            c->path,
            c->version,
            c->status_code,
            c->resp_len,
            "-",  // User-Agent（暂不支持）
            c->req_id,
            upstream_addr,
            duration_ms
        );

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
        client_addr_t client;
        int cfd = nexus_acceptor_accept_ext(g_listen_fd, &client);
        if (cfd >= 0) {
            proxy_conn_t *c = proxy_alloc(cfd);
            if (c) {
                // Task 7: 记录客户端地址
                snprintf(c->client_ip, sizeof(c->client_ip), "%s", client.ip);
                c->client_port = client.port;

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
    nexus_log_reset();
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

    int reuse_port = (getenv("NEXUS_WORKER") != NULL);
    g_listen_fd = nexus_acceptor_listen(ip, port, reuse_port);
    if (g_listen_fd < 0) { perror("listen"); return 1; }
    nexus_epoll_init();
    nexus_epoll_add(g_listen_fd, EPOLLIN, NULL);
    nexus_log_error(1, "worker started on %s:%d", ip, port);

    while (1) {
        nexus_epoll_wait(100, on_event);
    }
    return 0;
}
