#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nexus_config.h"
#include "nexus_master.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <config.ini>\n", argv[0]); return 1; }
    nexus_config_t *cfg = nexus_config_load(argv[1]);
    if (!cfg) { perror("config load"); return 1; }
    int rc = nexus_master_run(cfg, argv[1]);
    nexus_config_free(cfg);
    return rc;
}
