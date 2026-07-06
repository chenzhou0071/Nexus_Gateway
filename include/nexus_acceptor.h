#ifndef NEXUS_ACCEPTOR_H
#define NEXUS_ACCEPTOR_H

int nexus_acceptor_listen(const char *bind_ip, int port, int reuse_port);
int nexus_acceptor_accept(int listen_fd);

#endif
