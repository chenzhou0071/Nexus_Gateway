#include "nexus_blacklist.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    nexus_blacklist_t bl;
    nexus_bl_init(&bl);
    nexus_bl_add(&bl, "192.168.1.100");
    nexus_bl_add(&bl, "10.0.0.5");

    assert(nexus_bl_check(&bl, "192.168.1.100") == 1);
    assert(nexus_bl_check(&bl, "10.0.0.5") == 1);
    assert(nexus_bl_check(&bl, "8.8.8.8") == 0);

    nexus_bl_free(&bl);
    printf("test_blacklist: all passed\n");
    return 0;
}