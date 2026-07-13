#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const char *RESP_1KB =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 1023\r\n"
    "Connection: close\r\n"
    "\r\n";

static char payload[1024];

static int running = 1;

void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    memset(payload, 'A', sizeof(payload) - 1);
    payload[sizeof(payload) - 1] = '\0';

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19995);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    listen(srv, 1024);
    printf("Upstream server listening on 19995\n");

    while (running) {
        int cli = accept(srv, NULL, NULL);
        if (cli < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);
                continue;
            }
            break;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            char buf[4096];
            ssize_t n = read(cli, buf, sizeof(buf) - 1);
            if (n > 0) {
                ssize_t w1 = write(cli, RESP_1KB, strlen(RESP_1KB));
                ssize_t w2 = write(cli, payload, sizeof(payload) - 1);
                (void)w1; (void)w2;
            }
            close(cli);
            exit(0);
        } else if (pid > 0) {
            close(cli);
        } else {
            close(cli);
        }
    }

    close(srv);
    return 0;
}
