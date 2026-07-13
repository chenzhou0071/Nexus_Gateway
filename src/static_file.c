#include "nexus_static.h"
#include "nexus_zerocopy.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static const char *content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".txt") == 0)  return "text/plain";
    if (strcmp(dot, ".css") == 0)  return "text/css";
    if (strcmp(dot, ".js") == 0)   return "application/javascript";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    if (strcmp(dot, ".jpg") == 0)  return "image/jpeg";
    return "application/octet-stream";
}

int nexus_static_serve(int client_fd, const char *root, const char *path) {
    if (strstr(path, "..") != NULL) return -1;  // 防止路径穿越
    char full[1024];
    snprintf(full, sizeof(full), "%s%s", root, path);

    int fd = open(full, O_RDONLY);
    if (fd < 0) {
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        ssize_t wret = write(client_fd, resp, strlen(resp));
        (void)wret;
        return -1;
    }
    struct stat st;
    fstat(fd, &st);

    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        content_type(path), (long)st.st_size);
    ssize_t wret2 = write(client_fd, hdr, (size_t)hlen);
    (void)wret2;

    off_t off = 0;
    ssize_t remaining = st.st_size;
    while (remaining > 0) {
        ssize_t n = nexus_zc_sendfile(client_fd, fd, &off, (size_t)remaining);
        if (n <= 0) break;
        remaining -= n;
    }
    close(fd);
    return 0;
}