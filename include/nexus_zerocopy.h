#ifndef NEXUS_ZEROCOPY_H
#define NEXUS_ZEROCOPY_H

#include <sys/types.h>

ssize_t nexus_zc_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
ssize_t nexus_zc_splice(int from_fd, int to_fd, size_t len);

#endif