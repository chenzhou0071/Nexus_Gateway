#include "nexus_zerocopy.h"
#include <sys/sendfile.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

ssize_t nexus_zc_sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    return sendfile(out_fd, in_fd, offset, count);
}

ssize_t nexus_zc_splice(int from_fd, int to_fd, size_t len) {
    int pipefd[2];
    if (pipe2(pipefd, O_NONBLOCK) < 0) return -1;
    ssize_t total = 0;
    while (len > 0) {
        ssize_t n = splice(from_fd, NULL, pipefd[1], NULL, len, 0);
        if (n <= 0) break;
        ssize_t m = splice(pipefd[0], NULL, to_fd, NULL, n, 0);
        if (m <= 0) break;
        len -= m;
        total += m;
    }
    close(pipefd[0]); close(pipefd[1]);
    return total;
}