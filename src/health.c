#include "nexus_health.h"
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

struct nexus_health_checker_t {
    nexus_upstream_t *u;
    int              interval_sec;
    int              fail_threshold;
    atomic_int       running;
    pthread_t        tid;
    int              fail_count[16];
};

static int tcp_probe(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    int ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    close(fd);
    return ok;
}

static void *run(void *arg) {
    nexus_health_checker_t *h = arg;
    while (atomic_load(&h->running)) {
        for (int i = 0; i < h->u->node_count; i++) {
            int ok = tcp_probe(h->u->nodes[i].host, h->u->nodes[i].port);
            if (ok) {
                h->fail_count[i] = 0;
                h->u->nodes[i].healthy = 1;
            } else {
                if (++h->fail_count[i] >= h->fail_threshold) h->u->nodes[i].healthy = 0;
            }
        }
        sleep(h->interval_sec);
    }
    return NULL;
}

nexus_health_checker_t *nexus_health_create(nexus_upstream_t *u, int interval_sec, int fail_threshold) {
    nexus_health_checker_t *h = calloc(1, sizeof(*h));
    h->u = u; h->interval_sec = interval_sec; h->fail_threshold = fail_threshold;
    return h;
}

void nexus_health_start(nexus_health_checker_t *h) {
    atomic_store(&h->running, 1);
    pthread_create(&h->tid, NULL, run, h);
}

void nexus_health_stop(nexus_health_checker_t *h) {
    atomic_store(&h->running, 0);
    pthread_join(h->tid, NULL);
}

void nexus_health_destroy(nexus_health_checker_t *h) { free(h); }