#ifndef KOFVS_ROUTE_H
#define KOFVS_ROUTE_H

#define ROUTE_SLOTS 4

enum RouteKind {
    ROUTE_REAL,
    ROUTE_NEUTRAL,
    ROUTE_P1,
    ROUTE_P2,
    ROUTE_DISCONNECTED
};

int route_port_of(const int* portmap, int index);
int route_decide(int access, const int* portmap, int index, int p1_present, int p2_present);
int route_slot_for_port(const int* portmap, int port);

#endif
