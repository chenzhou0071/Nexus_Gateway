#include "nexus_epoll.h"
#include <assert.h>
#include <unistd.h>
#include <string.h>

static int g_called = 0;
static int g_got_fd = 0;
static uint32_t g_got_events = 0;

static void on_event(int fd, uint32_t ev, void *u) {
    (void)u;
    g_called++;
    g_got_fd = fd;
    g_got_events = ev;
}

int main(void) {
    assert(nexus_epoll_init() == 0);

    int p[2];
    assert(pipe(p) == 0);

    nexus_epoll_add(p[0], EPOLLIN, NULL);
    write(p[1], "x", 1);

    int n = nexus_epoll_wait(100, on_event);
    assert(n >= 1);
    assert(g_called >= 1);
    assert(g_got_fd == p[0]);
    assert(g_got_events & EPOLLIN);

    nexus_epoll_close();
    close(p[0]); close(p[1]);
    printf("test_epoll: all passed\n");
    return 0;
}
