#ifndef NEXUS_PROXY_H
#define NEXUS_PROXY_H

#include "nexus_upstream.h"
#include <stddef.h>

int nexus_proxy_connect_upstream(const nexus_upstream_node_t *node);
int nexus_proxy_forward_request(int upstream_fd, const char *req, size_t len);
int nexus_proxy_read_response(int upstream_fd, char *buf, size_t cap);

#endif
