#include "nexus_epoll.h"
#include "nexus_util.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

#define MAX_EVENTS 1024

static int g_epfd = -1;
static struct epoll_event g_events[MAX_EVENTS];
static void *g_user_data[MAX_EVENTS];

int nexus_epoll_init(void) {
    g_epfd = epoll_create1(EPOLL_CLOEXEC);
    return g_epfd < 0 ? -1 : 0;
}

void nexus_epoll_add(int fd, uint32_t events, void *user) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev);
    g_user_data[fd % MAX_EVENTS] = user;
}

void nexus_epoll_mod(int fd, uint32_t events, void *user) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(g_epfd, EPOLL_CTL_MOD, fd, &ev);
    g_user_data[fd % MAX_EVENTS] = user;
}

void nexus_epoll_del(int fd) {
    epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, NULL);
    g_user_data[fd % MAX_EVENTS] = NULL;
}

int nexus_epoll_wait(int timeout_ms, nexus_event_cb cb) {
    int n = epoll_wait(g_epfd, g_events, MAX_EVENTS, timeout_ms);
    for (int i = 0; i < n; i++) {
        int fd = g_events[i].data.fd;
        cb(fd, g_events[i].events, g_user_data[fd % MAX_EVENTS]);
    }
    return n;
}

void nexus_epoll_close(void) {
    if (g_epfd >= 0) { close(g_epfd); g_epfd = -1; }
}
