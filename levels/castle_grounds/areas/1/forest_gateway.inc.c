// Optional SM64-native woodland trail and timber gateway to Hyrule Field.
// This original geometry is only linked when IOS_HYRULE_WORLD is enabled.
static const Vtx sForestGatewayTrailVertices[] = {
    {{{-7420, 263,  720}, 0, {0, 0}, {0x2A, 0x3D, 0x2B, 0xFF}}},
    {{{-7420, 263, 1100}, 0, {0, 0}, {0x6A, 0x4B, 0x2E, 0xFF}}},
    {{{-7420, 263, 1820}, 0, {0, 0}, {0x71, 0x51, 0x31, 0xFF}}},
    {{{-7420, 263, 2200}, 0, {0, 0}, {0x2D, 0x43, 0x2B, 0xFF}}},
    {{{-8600, 263,  720}, 0, {0, 0}, {0x32, 0x48, 0x30, 0xFF}}},
    {{{-8600, 263, 1100}, 0, {0, 0}, {0x75, 0x53, 0x32, 0xFF}}},
    {{{-8600, 263, 1820}, 0, {0, 0}, {0x7A, 0x58, 0x35, 0xFF}}},
    {{{-8600, 263, 2200}, 0, {0, 0}, {0x32, 0x49, 0x30, 0xFF}}},
    {{{-9800, 263,  720}, 0, {0, 0}, {0x2B, 0x41, 0x2A, 0xFF}}},
    {{{-9800, 263, 1100}, 0, {0, 0}, {0x70, 0x4E, 0x31, 0xFF}}},
    {{{-9800, 263, 1820}, 0, {0, 0}, {0x76, 0x54, 0x34, 0xFF}}},
    {{{-9800, 263, 2200}, 0, {0, 0}, {0x2C, 0x42, 0x2A, 0xFF}}},
    {{{-11100, 263,  720}, 0, {0, 0}, {0x2A, 0x3E, 0x28, 0xFF}}},
    {{{-11100, 263, 1100}, 0, {0, 0}, {0x68, 0x49, 0x2D, 0xFF}}},
    {{{-11100, 263, 1820}, 0, {0, 0}, {0x70, 0x50, 0x30, 0xFF}}},
    {{{-11100, 263, 2200}, 0, {0, 0}, {0x2B, 0x40, 0x29, 0xFF}}},
};

// A simple open-sided timber frame marks the route without a portal or menu cue.
static const Vtx sForestGatewayArchVertices[] = {
    {{{-7520, 260,  760}, 0, {0, 0}, {0x62, 0x39, 0x20, 0xFF}}},
    {{{-7520, 1080, 760}, 0, {0, 0}, {0x62, 0x39, 0x20, 0xFF}}},
    {{{-7520, 260,  950}, 0, {0, 0}, {0x78, 0x4B, 0x29, 0xFF}}},
    {{{-7520, 1080, 950}, 0, {0, 0}, {0x78, 0x4B, 0x29, 0xFF}}},
    {{{-7520, 260, 1970}, 0, {0, 0}, {0x78, 0x4B, 0x29, 0xFF}}},
    {{{-7520, 1080, 1970}, 0, {0, 0}, {0x78, 0x4B, 0x29, 0xFF}}},
    {{{-7520, 260, 2160}, 0, {0, 0}, {0x62, 0x39, 0x20, 0xFF}}},
    {{{-7520, 1080, 2160}, 0, {0, 0}, {0x62, 0x39, 0x20, 0xFF}}},
    {{{-7520, 1010,  950}, 0, {0, 0}, {0x4D, 0x2F, 0x1E, 0xFF}}},
    {{{-7520, 1220,  950}, 0, {0, 0}, {0x5B, 0x36, 0x20, 0xFF}}},
    {{{-7520, 1010, 1970}, 0, {0, 0}, {0x5B, 0x36, 0x20, 0xFF}}},
    {{{-7520, 1220, 1970}, 0, {0, 0}, {0x4D, 0x2F, 0x1E, 0xFF}}},
};

const Gfx castle_grounds_forest_gateway_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPVertex(sForestGatewayTrailVertices, 16, 0),
    gsSP2Triangles(0, 4, 1, 0, 4, 5, 1, 0),
    gsSP2Triangles(1, 5, 2, 0, 5, 6, 2, 0),
    gsSP2Triangles(2, 6, 3, 0, 6, 7, 3, 0),
    gsSP2Triangles(4, 8, 5, 0, 8, 9, 5, 0),
    gsSP2Triangles(5, 9, 6, 0, 9, 10, 6, 0),
    gsSP2Triangles(6, 10, 7, 0, 10, 11, 7, 0),
    gsSP2Triangles(8, 12, 9, 0, 12, 13, 9, 0),
    gsSP2Triangles(9, 13, 10, 0, 13, 14, 10, 0),
    gsSP2Triangles(10, 14, 11, 0, 14, 15, 11, 0),
    gsSPVertex(sForestGatewayArchVertices, 12, 0),
    gsSP2Triangles(0, 1, 2, 0, 1, 3, 2, 0),
    gsSP2Triangles(4, 5, 6, 0, 5, 7, 6, 0),
    gsSP2Triangles(8, 9, 10, 0, 9, 11, 10, 0),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPPipeSync(),
    gsSPEndDisplayList(),
};
