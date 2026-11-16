// Compile-time region registry for this single Castle Grounds Area.
// Each regional module owns its vertex/display data; this root is the only
// node the Castle geo layout needs as the connected world grows.
#include "levels/castle_grounds/areas/1/connected_forest.inc.c"
#include "levels/castle_grounds/areas/1/connected_world/north_transition.inc.c"

const Gfx castle_grounds_connected_world_dl[] = {
    gsDPPipeSync(),
    gsSPDisplayList(castle_grounds_connected_forest_dl),
    gsSPDisplayList(castle_grounds_north_transition_dl),
    gsDPPipeSync(),
    gsSPEndDisplayList(),
};
