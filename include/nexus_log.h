#ifndef NEXUS_LOG_H
#define NEXUS_LOG_H

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

int  nexus_log_init(const char *log_dir, int log_level);
void nexus_log_close(void);
void nexus_log_access(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void nexus_log_error(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// 重置日志句柄（用于子进程关闭从父进程继承的 fp）
void nexus_log_reset(void);

// Master 专用日志（写独立的 master.log，进程内自带 pid 前缀）
int  nexus_log_init_master(const char *log_dir);
void nexus_log_master(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// 完整 access 日志（Nginx 风格格式）
void nexus_log_access_full(
    const char *client_ip,
    uint16_t   client_port,
    const char *method,
    const char *path,
    const char *version,
    int        status_code,
    size_t     bytes_sent,
    const char *user_agent,
    const char *req_id,
    const char *upstream_addr,
    double     duration_ms
);

#endif
