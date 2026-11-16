// Deterministic vertex-colored test extension for Mission 55.4.
// This is authored SM64 geometry, not imported Zelda content.
static const Vtx sConnectedForestExtensionVertices[] = {
    // Green clearing; its entrance edge is shared with the Castle trail.
    {{{-9800, 263, 2200}, 0, {0, 0}, {0x35, 0x68, 0x34, 0xFF}}},
    {{{-8600, 263, 2200}, 0, {0, 0}, {0x3D, 0x72, 0x39, 0xFF}}},
    {{{-10400, 263, 4200}, 0, {0, 0}, {0x2C, 0x5C, 0x2E, 0xFF}}},
    {{{-8200, 263, 4200}, 0, {0, 0}, {0x3A, 0x68, 0x32, 0xFF}}},
    // A dirt path widens with the clearing so its destination is visible.
    {{{-9500, 264, 2200}, 0, {0, 0}, {0x72, 0x51, 0x32, 0xFF}}},
    {{{-8900, 264, 2200}, 0, {0, 0}, {0x78, 0x56, 0x35, 0xFF}}},
    {{{-10000, 264, 4200}, 0, {0, 0}, {0x68, 0x49, 0x2D, 0xFF}}},
    {{{-8400, 264, 4200}, 0, {0, 0}, {0x72, 0x50, 0x30, 0xFF}}},
};

const Gfx castle_grounds_connected_forest_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPVertex(sConnectedForestExtensionVertices, 8, 0),
    gsSP2Triangles(0, 2, 1, 0, 2, 3, 1, 0),
    gsSP2Triangles(4, 6, 5, 0, 6, 7, 5, 0),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPPipeSync(),
    gsSPEndDisplayList(),
};
