#include "route.h"
#include "selector.h"

int route_port_of(const int* portmap, int index) {
    int v;
    if (index < 0 || index >= ROUTE_SLOTS) return 0;
    if (!portmap) return index + 1;
    v = portmap[index];
    if (v < 0) return 0;
    return v == 0 ? index + 1 : v;
}

int route_decide(int access, const int* portmap, int index, int p1_present, int p2_present) {
    int port;
    if (access == ACCESS_PASSTHROUGH) return ROUTE_REAL;
    port = route_port_of(portmap, index);
    if (access == ACCESS_BLOCK_ALL) return (port == 1 || port == 2) ? ROUTE_NEUTRAL : ROUTE_DISCONNECTED;
    if (port == 1) return p1_present ? ROUTE_P1 : ROUTE_NEUTRAL;
    if (port == 2) return p2_present ? ROUTE_P2 : ROUTE_NEUTRAL;
    return ROUTE_DISCONNECTED;
}

int route_slot_for_port(const int* portmap, int port) {
    int i;
    for (i = 0; i < ROUTE_SLOTS; ++i)
        if (route_port_of(portmap, i) == port) return i;
    return -1;
}
