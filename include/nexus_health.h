#ifndef NEXUS_HEALTH_H
#define NEXUS_HEALTH_H

#include "nexus_upstream.h"

typedef struct nexus_health_checker_t nexus_health_checker_t;

nexus_health_checker_t *nexus_health_create(nexus_upstream_t *u, int interval_sec, int fail_threshold);
void nexus_health_start(nexus_health_checker_t *h);
void nexus_health_stop(nexus_health_checker_t *h);
void nexus_health_destroy(nexus_health_checker_t *h);

#endif