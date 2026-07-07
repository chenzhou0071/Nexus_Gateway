#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "nexus_config.h"
#include "nexus_worker.h"
#include "nexus_util.h"

int main(int argc, char **argv) {
    // Task 7: 初始化随机种子（多 Worker 独立）
    srand((unsigned)(getpid() ^ nexus_now_us()));

    if (argc < 2) { fprintf(stderr, "usage: %s <config.ini>\n", argv[0]); return 1; }
    nexus_config_t *cfg = nexus_config_load(argv[1]);
    if (!cfg) { perror("config load"); return 1; }
    int rc = nexus_worker_run(cfg);
    nexus_config_free(cfg);
    return rc;
}
