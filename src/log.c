#define _POSIX_C_SOURCE 200809L
#include "nexus_log.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

static FILE *g_access_fp = NULL;
static FILE *g_error_fp = NULL;
static int   g_log_level = 1;
static pthread_mutex_t g_access_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_error_mu  = PTHREAD_MUTEX_INITIALIZER;

int nexus_log_init(const char *log_dir, int log_level) {
    mkdir(log_dir, 0755);
    char path[512];
    snprintf(path, sizeof(path), "%s/access.log", log_dir);
    g_access_fp = fopen(path, "a");
    snprintf(path, sizeof(path), "%s/error.log", log_dir);
    g_error_fp = fopen(path, "a");
    g_log_level = log_level;
    if (!g_access_fp || !g_error_fp) return -1;
    setvbuf(g_access_fp, NULL, _IOLBF, 0);
    setvbuf(g_error_fp,  NULL, _IOLBF, 0);
    return 0;
}

void nexus_log_close(void) {
    if (g_access_fp) { fclose(g_access_fp); g_access_fp = NULL; }
    if (g_error_fp)  { fclose(g_error_fp);  g_error_fp  = NULL; }
}

static void format_ts(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

void nexus_log_access(const char *fmt, ...) {
    if (!g_access_fp) return;
    char ts[32];
    format_ts(ts, sizeof(ts));
    pthread_mutex_lock(&g_access_mu);
    fprintf(g_access_fp, "[%s] ", ts);
    va_list ap; va_start(ap, fmt); vfprintf(g_access_fp, fmt, ap); va_end(ap);
    fputc('\n', g_access_fp);
    pthread_mutex_unlock(&g_access_mu);
}

void nexus_log_error(int level, const char *fmt, ...) {
    if (!g_error_fp || level < g_log_level) return;
    const char *tag = (level >= 3) ? "ERROR" : (level == 2 ? "WARN " : "INFO ");
    char ts[32];
    format_ts(ts, sizeof(ts));
    pthread_mutex_lock(&g_error_mu);
    fprintf(g_error_fp, "[%s] [%s] ", ts, tag);
    va_list ap; va_start(ap, fmt); vfprintf(g_error_fp, fmt, ap); va_end(ap);
    fputc('\n', g_error_fp);
    pthread_mutex_unlock(&g_error_mu);
}
