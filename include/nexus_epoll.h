#ifndef NEXUS_EPOLL_H
#define NEXUS_EPOLL_H

#include <stdint.h>
#include <sys/epoll.h>

typedef void (*nexus_event_cb)(int fd, uint32_t events, void *user);

int  nexus_epoll_init(void);
void nexus_epoll_add(int fd, uint32_t events, void *user);
void nexus_epoll_mod(int fd, uint32_t events, void *user);
void nexus_epoll_del(int fd);
int  nexus_epoll_wait(int timeout_ms, nexus_event_cb cb);
void nexus_epoll_close(void);

#endif
