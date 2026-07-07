#define _POSIX_C_SOURCE 200809L
#include "nexus_master.h"
#include "nexus_worker.h"
#include "nexus_util.h"
#include "nexus_log.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

config_shadow_t *g_shadow = NULL;
atomic_int      *g_active_slot = NULL;
atomic_int        g_shutting_down = 0;

static volatile sig_atomic_t g_sighup = 0;
static char g_config_path[512];

static void on_sighup(int s) { (void)s; g_sighup = 1; }
static void on_sigchld(int s) { (void)s; }

static void setup_shared_memory(void) {
    int fd = shm_open("/nexus_config", O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        perror("shm_open");
        exit(1);
    }

    size_t total = sizeof(config_shadow_t) * 2 + sizeof(atomic_int);
    if (ftruncate(fd, (off_t)total) < 0) {
        perror("ftruncate");
        close(fd);
        exit(1);
    }

    g_shadow = mmap(NULL, sizeof(config_shadow_t) * 2,
                    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g_shadow == MAP_FAILED) {
        perror("mmap shadow");
        close(fd);
        exit(1);
    }

    g_active_slot = (atomic_int *)((char *)g_shadow + sizeof(config_shadow_t) * 2);
    atomic_store(g_active_slot, 0);
    atomic_store(&g_shutting_down, 0);

    memset(g_shadow, 0, sizeof(config_shadow_t) * 2);

    close(fd);
}

static void serialize_config(nexus_config_t *cfg, char *buf, uint32_t *size_out) {
    char *p = buf;
    char *end = buf + CONFIG_MAX - 1;

    const char *listen = nexus_config_get(cfg, "server", "listen");
    const char *log_dir = "logs";
    const char *log_level = nexus_config_get(cfg, "server", "log_level");
    const char *worker_num = nexus_config_get(cfg, "server", "worker_num");

    int n = 0;
    n += snprintf(p + n, end - p - n, "listen=%s\n",     listen     ? listen     : "0.0.0.0:8080");
    n += snprintf(p + n, end - p - n, "log_dir=%s\n",    log_dir);
    n += snprintf(p + n, end - p - n, "log_level=%s\n",  log_level   ? log_level   : "info");
    n += snprintf(p + n, end - p - n, "worker_num=%s\n", worker_num  ? worker_num  : "auto");

    const char *upstream_srv = nexus_config_get(cfg, "upstream.api", "server");
    if (upstream_srv) {
        n += snprintf(p + n, end - p - n, "upstream.api.server=%s\n", upstream_srv);
    }

    *size_out = (uint32_t)n;
}

static void reload_config(nexus_config_t *cfg) {
    int inactive = 1 - atomic_load(g_active_slot);
    serialize_config(cfg, g_shadow[inactive].data, &g_shadow[inactive].total_size);
    __sync_synchronize();
    atomic_fetch_add(&g_shadow[inactive].version, 1);
    atomic_store(g_active_slot, inactive);
    nexus_log_master(1, "config reloaded, slot=%d, version=%lu",
                    inactive, (unsigned long)g_shadow[inactive].version);
}

static int g_worker_count = 0;
static pid_t *g_worker_pids = NULL;

static pid_t spawn_worker(nexus_config_t *cfg) {
    pid_t pid = fork();
    if (pid == 0) {
        setenv("NEXUS_WORKER", "1", 1);
        nexus_worker_run(cfg);
        exit(0);
    }
    return pid;
}

int nexus_master_run(nexus_config_t *cfg, const char *config_path) {
    snprintf(g_config_path, sizeof(g_config_path), "%s", config_path);
    setup_shared_memory();

    serialize_config(cfg, g_shadow[0].data, &g_shadow[0].total_size);
    g_shadow[0].version = 1;

    struct sigaction sa_hup = {0};
    sa_hup.sa_handler = on_sighup;
    sigaction(SIGHUP, &sa_hup, NULL);

    struct sigaction sa_chld = {0};
    sa_chld.sa_handler = on_sigchld;
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    signal(SIGPIPE, SIG_IGN);

    const char *wn = nexus_config_get(cfg, "server", "worker_num");
    if (wn && strcmp(wn, "auto") != 0) {
        g_worker_count = atoi(wn);
    } else {
        g_worker_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (g_worker_count < 1) g_worker_count = 1;
    }
    if (g_worker_count < 1) g_worker_count = 1;
    if (g_worker_count > 64) g_worker_count = 64;

    g_worker_pids = calloc((size_t)g_worker_count, sizeof(pid_t));

    nexus_log_init_master("logs");
    nexus_log_master(1, "master started, workers=%d", g_worker_count);

    for (int i = 0; i < g_worker_count; i++) {
        g_worker_pids[i] = spawn_worker(cfg);
    }
    nexus_log_master(1, "spawned %d workers", g_worker_count);

    while (1) {
        if (g_sighup) {
            g_sighup = 0;
            nexus_config_t *new_cfg = nexus_config_load(g_config_path);
            if (new_cfg) {
                reload_config(new_cfg);
                nexus_config_free(new_cfg);
            } else {
                nexus_log_master(2, "config reload failed, keeping old");
            }
        }

        for (int i = 0; i < g_worker_count; i++) {
            int status;
            pid_t r = waitpid(g_worker_pids[i], &status, WNOHANG);
            if (r == g_worker_pids[i]) {
                nexus_log_master(2, "worker %d died, respawning", g_worker_pids[i]);
                g_worker_pids[i] = spawn_worker(cfg);
                nexus_log_master(1, "respawned pid=%d", g_worker_pids[i]);
            } else if (r < 0 && errno != EAGAIN && errno != EINTR) {
                nexus_log_master(2, "waitpid error on worker %d: %s",
                                g_worker_pids[i], strerror(errno));
            }
        }

        sleep(1);
    }
    return 0;
}
