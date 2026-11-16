// Narrow, rising trail to an open attachment point for a future region.
// The far edge is deliberately open: it is not a portal, warp, or level seam.
static const Vtx sConnectedNorthGroundVertices[] = {
    {{{-9800, 443, 8000}, 0, {0, 0}, {0x34, 0x5C, 0x30, 0xFF}}},
    {{{-8800, 423, 8000}, 0, {0, 0}, {0x3D, 0x66, 0x34, 0xFF}}},
    {{{-9800, 503, 9800}, 0, {0, 0}, {0x2D, 0x53, 0x2D, 0xFF}}},
    {{{-8800, 483, 9800}, 0, {0, 0}, {0x36, 0x61, 0x32, 0xFF}}},
    {{{-9600, 563, 11600}, 0, {0, 0}, {0x2B, 0x4B, 0x2C, 0xFF}}},
    {{{-8800, 543, 11600}, 0, {0, 0}, {0x38, 0x5B, 0x31, 0xFF}}},
};

static const Vtx sConnectedNorthTrailVertices[] = {
    {{{-9700, 441, 8000}, 0, {0, 0}, {0x70, 0x50, 0x30, 0xFF}}},
    {{{-9100, 429, 8000}, 0, {0, 0}, {0x78, 0x56, 0x34, 0xFF}}},
    {{{-9700, 501, 9800}, 0, {0, 0}, {0x73, 0x52, 0x32, 0xFF}}},
    {{{-9100, 489, 9800}, 0, {0, 0}, {0x7A, 0x59, 0x37, 0xFF}}},
    {{{-9500, 561, 11600}, 0, {0, 0}, {0x6E, 0x4E, 0x30, 0xFF}}},
    {{{-9000, 548, 11600}, 0, {0, 0}, {0x76, 0x54, 0x33, 0xFF}}},
};

// A modest timber marker gives the rising path a destination landmark while
// leaving a generous opening for Mario and the kart.
static const Vtx sConnectedNorthMarkerVertices[] = {
    {{{-9650, 532, 10800}, 0, {0, 0}, {0x55, 0x37, 0x20, 0xFF}}},
    {{{-9550, 532, 10800}, 0, {0, 0}, {0x6B, 0x47, 0x29, 0xFF}}},
    {{{-9650, 892, 10800}, 0, {0, 0}, {0x5C, 0x3C, 0x23, 0xFF}}},
    {{{-9550, 892, 10800}, 0, {0, 0}, {0x75, 0x4B, 0x2B, 0xFF}}},
    {{{-8950, 518, 10800}, 0, {0, 0}, {0x75, 0x4B, 0x29, 0xFF}}},
    {{{-8850, 515, 10800}, 0, {0, 0}, {0x5C, 0x3C, 0x23, 0xFF}}},
    {{{-8950, 878, 10800}, 0, {0, 0}, {0x6B, 0x47, 0x29, 0xFF}}},
    {{{-8850, 875, 10800}, 0, {0, 0}, {0x55, 0x37, 0x20, 0xFF}}},
    {{{-9550, 842, 10800}, 0, {0, 0}, {0x68, 0x43, 0x27, 0xFF}}},
    {{{-8950, 828, 10800}, 0, {0, 0}, {0x68, 0x43, 0x27, 0xFF}}},
    {{{-9550, 892, 10800}, 0, {0, 0}, {0x7A, 0x50, 0x2F, 0xFF}}},
    {{{-8950, 878, 10800}, 0, {0, 0}, {0x7A, 0x50, 0x2F, 0xFF}}},
};

const Gfx castle_grounds_north_transition_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPVertex(sConnectedNorthGroundVertices, 6, 0),
    gsSP2Triangles(0, 2, 1, 0, 2, 3, 1, 0),
    gsSP2Triangles(2, 4, 3, 0, 4, 5, 3, 0),
    gsSPVertex(sConnectedNorthTrailVertices, 6, 0),
    gsSP2Triangles(0, 2, 1, 0, 2, 3, 1, 0),
    gsSP2Triangles(2, 4, 3, 0, 4, 5, 3, 0),
    gsSPVertex(sConnectedNorthMarkerVertices, 12, 0),
    gsSP2Triangles(0, 1, 2, 0, 1, 3, 2, 0),
    gsSP2Triangles(4, 5, 6, 0, 5, 7, 6, 0),
    gsSP2Triangles(8, 10, 9, 0, 10, 11, 9, 0),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPPipeSync(),
    gsSPEndDisplayList(),
};
