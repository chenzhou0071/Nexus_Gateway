#include <sys/sendfile.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int main() {
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    
    int fd = open("/tmp/test_sendfile.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }
    
    const char *buf = "Hello World (12 bytes)";
    write(sv[1], buf, 20);
    
    off_t off = 0;
    ssize_t n = sendfile(fd, sv[0], &off, 20);
    if (n < 0) {
        perror("sendfile socket->file");
        printf("errno=%d\n", errno);
        return 1;
    }
    
    printf("sendfile OK: copied %zd bytes\n", n);
    close(fd); close(sv[0]); close(sv[1]);
    return 0;
}
