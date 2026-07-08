#include "nexus_zerocopy.h"
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

int main(void) {
    // 准备源文件
    const char *content = "zero-copy test payload\n";
    int src = open("/tmp/nexus_zc_src.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ssize_t wret = write(src, content, strlen(content));
    (void)wret;  // 测试代码忽略返回值
    close(src);

    int fdin = open("/tmp/nexus_zc_src.txt", O_RDONLY);
    int fdout = open("/tmp/nexus_zc_dst.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    off_t off = 0;
    ssize_t n = nexus_zc_sendfile(fdout, fdin, &off, strlen(content));
    assert(n == (ssize_t)strlen(content));
    close(fdin); close(fdout);

    int fdr = open("/tmp/nexus_zc_dst.txt", O_RDONLY);
    char buf[64] = {0};
    ssize_t rret = read(fdr, buf, sizeof(buf) - 1);
    (void)rret;  // 测试代码忽略返回值
    assert(strcmp(buf, content) == 0);
    close(fdr);

    unlink("/tmp/nexus_zc_src.txt");
    unlink("/tmp/nexus_zc_dst.txt");
    printf("test_zerocopy: all passed\n");
    return 0;
}