#ifndef NEXUS_LOG_H
#define NEXUS_LOG_H

#include <stdarg.h>

int  nexus_log_init(const char *log_dir, int log_level);
void nexus_log_close(void);
void nexus_log_access(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void nexus_log_error(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif
