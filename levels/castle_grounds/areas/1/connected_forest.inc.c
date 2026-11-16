// Original, vertex-colored Castle Grounds forest extension. The geometry is
// intentionally low-poly and has no dependency on Zelda or other game assets.

static const Vtx sConnectedClearingVertices[] = {
    {{{-9800, 263, 2200}, 0, {0, 0}, {0x35, 0x68, 0x34, 0xFF}}},
    {{{-8600, 263, 2200}, 0, {0, 0}, {0x3D, 0x72, 0x39, 0xFF}}},
    {{{-10400, 323, 4200}, 0, {0, 0}, {0x2C, 0x5C, 0x2E, 0xFF}}},
    {{{-8200, 323, 4200}, 0, {0, 0}, {0x3A, 0x68, 0x32, 0xFF}}},
    {{{-9500, 264, 2200}, 0, {0, 0}, {0x72, 0x51, 0x32, 0xFF}}},
    {{{-8900, 264, 2200}, 0, {0, 0}, {0x78, 0x56, 0x35, 0xFF}}},
    {{{-10000, 324, 4200}, 0, {0, 0}, {0x68, 0x49, 0x2D, 0xFF}}},
    {{{-8400, 324, 4200}, 0, {0, 0}, {0x72, 0x50, 0x30, 0xFF}}},
};

// Three-column hills are a small grid rather than a giant triangle fan. Their
// vertex heights match the collision stations, with a 3-unit visual offset.
static const Vtx sConnectedMeadowVertices[] = {
    {{{-10400, 323, 4200}, 0, {0, 0}, {0x2C, 0x5C, 0x2E, 0xFF}}},
    {{{-9800, 323, 4200}, 0, {0, 0}, {0x36, 0x69, 0x35, 0xFF}}},
    {{{-8800, 323, 4200}, 0, {0, 0}, {0x3A, 0x69, 0x35, 0xFF}}},
    {{{-8200, 323, 4200}, 0, {0, 0}, {0x3A, 0x68, 0x32, 0xFF}}},
    {{{-10800, 383, 6200}, 0, {0, 0}, {0x2A, 0x55, 0x2C, 0xFF}}},
    {{{-9800, 373, 6200}, 0, {0, 0}, {0x3A, 0x6B, 0x36, 0xFF}}},
    {{{-8800, 353, 6200}, 0, {0, 0}, {0x43, 0x73, 0x3A, 0xFF}}},
    {{{-8200, 343, 6200}, 0, {0, 0}, {0x36, 0x60, 0x31, 0xFF}}},
    {{{-11000, 443, 8000}, 0, {0, 0}, {0x2C, 0x52, 0x2A, 0xFF}}},
    {{{-9800, 443, 8000}, 0, {0, 0}, {0x3B, 0x69, 0x35, 0xFF}}},
    {{{-8800, 423, 8000}, 0, {0, 0}, {0x45, 0x70, 0x38, 0xFF}}},
    {{{-8000, 403, 8000}, 0, {0, 0}, {0x32, 0x56, 0x2D, 0xFF}}},
};

static const Vtx sConnectedMainTrailVertices[] = {
    {{{-9800, 323, 4200}, 0, {0, 0}, {0x70, 0x50, 0x30, 0xFF}}},
    {{{-8800, 323, 4200}, 0, {0, 0}, {0x78, 0x56, 0x34, 0xFF}}},
    {{{-9700, 371, 6200}, 0, {0, 0}, {0x73, 0x52, 0x32, 0xFF}}},
    {{{-9100, 359, 6200}, 0, {0, 0}, {0x7A, 0x59, 0x37, 0xFF}}},
    {{{-9700, 441, 8000}, 0, {0, 0}, {0x6E, 0x4E, 0x30, 0xFF}}},
    {{{-9100, 429, 8000}, 0, {0, 0}, {0x76, 0x54, 0x33, 0xFF}}},
};

// A dirt loop branches west from the main trail and rejoins it. Each station
// is lifted three units above the same collision surface used by Mario.
static const Vtx sConnectedLoopTrailVertices[] = {
    {{{-9810, 372, 6160}, 0, {0, 0}, {0x6E, 0x4E, 0x30, 0xFF}}},
    {{{-9590, 370, 6240}, 0, {0, 0}, {0x76, 0x54, 0x33, 0xFF}}},
    {{{-10530, 399, 6780}, 0, {0, 0}, {0x70, 0x50, 0x31, 0xFF}}},
    {{{-10270, 398, 6820}, 0, {0, 0}, {0x7A, 0x58, 0x35, 0xFF}}},
    {{{-10640, 417, 7300}, 0, {0, 0}, {0x76, 0x53, 0x32, 0xFF}}},
    {{{-10360, 416, 7300}, 0, {0, 0}, {0x6D, 0x4D, 0x2E, 0xFF}}},
    {{{-10240, 432, 7720}, 0, {0, 0}, {0x72, 0x51, 0x31, 0xFF}}},
    {{{-9960, 431, 7680}, 0, {0, 0}, {0x7A, 0x58, 0x35, 0xFF}}},
    {{{-9800, 443, 8000}, 0, {0, 0}, {0x70, 0x50, 0x30, 0xFF}}},
    {{{-9600, 439, 8000}, 0, {0, 0}, {0x78, 0x56, 0x34, 0xFF}}},
};

// A crossed pair of simple tree planes gives the perimeter a readable
// silhouette without textures, lighting dependencies, or extra objects.
#define CONNECTED_PINE(x, y, z) \
    {{{(x) - 25, (y),       (z) - 20}, 0, {0, 0}, {0x55, 0x3D, 0x25, 0xFF}}}, \
    {{{(x) + 25, (y),       (z) - 20}, 0, {0, 0}, {0x55, 0x3D, 0x25, 0xFF}}}, \
    {{{(x) - 25, (y) + 150, (z) - 20}, 0, {0, 0}, {0x5E, 0x43, 0x28, 0xFF}}}, \
    {{{(x) + 25, (y) + 150, (z) - 20}, 0, {0, 0}, {0x5E, 0x43, 0x28, 0xFF}}}, \
    {{{(x) - 130, (y) + 150, (z) - 20}, 0, {0, 0}, {0x1D, 0x48, 0x28, 0xFF}}}, \
    {{{(x) + 130, (y) + 150, (z) - 20}, 0, {0, 0}, {0x31, 0x68, 0x35, 0xFF}}}, \
    {{{(x),        (y) + 610, (z) - 20}, 0, {0, 0}, {0x48, 0x79, 0x3B, 0xFF}}}, \
    {{{(x) - 20, (y),       (z) - 25}, 0, {0, 0}, {0x55, 0x3D, 0x25, 0xFF}}}, \
    {{{(x) - 20, (y),       (z) + 25}, 0, {0, 0}, {0x55, 0x3D, 0x25, 0xFF}}}, \
    {{{(x) - 20, (y) + 610, (z)},      0, {0, 0}, {0x3B, 0x70, 0x37, 0xFF}}}

static const Vtx sConnectedPineVertices[] = {
    CONNECTED_PINE(-10500, 363, 5600),
    CONNECTED_PINE(-10700, 419, 7400),
    CONNECTED_PINE(-8300, 337, 5600),
    CONNECTED_PINE(-8100, 382, 7400),
};
#undef CONNECTED_PINE

// Original stone marker beside the loop; decorative only, so it does not add
// a narrow collision obstacle to the Mario/kart route.
static const Vtx sConnectedLandmarkVertices[] = {
    {{{-10430, 413, 7270}, 0, {0, 0}, {0x54, 0x50, 0x47, 0xFF}}},
    {{{-10170, 413, 7270}, 0, {0, 0}, {0x69, 0x64, 0x58, 0xFF}}},
    {{{-10430, 423, 7530}, 0, {0, 0}, {0x5D, 0x58, 0x4E, 0xFF}}},
    {{{-10170, 423, 7530}, 0, {0, 0}, {0x75, 0x6F, 0x62, 0xFF}}},
    {{{-10400, 620, 7300}, 0, {0, 0}, {0x68, 0x63, 0x58, 0xFF}}},
    {{{-10200, 620, 7300}, 0, {0, 0}, {0x7A, 0x74, 0x68, 0xFF}}},
    {{{-10400, 620, 7500}, 0, {0, 0}, {0x5E, 0x59, 0x4E, 0xFF}}},
    {{{-10200, 620, 7500}, 0, {0, 0}, {0x70, 0x6A, 0x5F, 0xFF}}},
    {{{-10300, 800, 7400}, 0, {0, 0}, {0x86, 0x7F, 0x70, 0xFF}}},
};

const Gfx castle_grounds_connected_forest_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPVertex(sConnectedClearingVertices, 4, 0),
    gsSP2Triangles(0, 2, 1, 0, 2, 3, 1, 0),
    gsSPVertex(&sConnectedClearingVertices[4], 4, 0),
    gsSP2Triangles(0, 2, 1, 0, 2, 3, 1, 0),
    gsSPVertex(sConnectedMeadowVertices, 12, 0),
    gsSP2Triangles(0, 4, 1, 0, 4, 5, 1, 0),
    gsSP2Triangles(1, 5, 2, 0, 5, 6, 2, 0),
    gsSP2Triangles(2, 6, 3, 0, 6, 7, 3, 0),
    gsSP2Triangles(4, 8, 5, 0, 8, 9, 5, 0),
    gsSP2Triangles(5, 9, 6, 0, 9, 10, 6, 0),
    gsSP2Triangles(6, 10, 7, 0, 10, 11, 7, 0),
    gsSPVertex(sConnectedMainTrailVertices, 6, 0),
    gsSP2Triangles(0, 2, 1, 0, 2, 3, 1, 0),
    gsSP2Triangles(2, 4, 3, 0, 4, 5, 3, 0),
    gsSPVertex(sConnectedLoopTrailVertices, 10, 0),
    gsSP2Triangles(0, 2, 1, 0, 2, 3, 1, 0),
    gsSP2Triangles(2, 4, 3, 0, 4, 5, 3, 0),
    gsSP2Triangles(4, 6, 5, 0, 6, 7, 5, 0),
    gsSP2Triangles(6, 8, 7, 0, 8, 9, 7, 0),
    gsSPVertex(sConnectedPineVertices, 20, 0),
    gsSP2Triangles(0, 1, 2, 0, 1, 3, 2, 0),
    gsSP1Triangle(4, 5, 6, 0),
    gsSP1Triangle(7, 8, 9, 0),
    gsSP2Triangles(10, 11, 12, 0, 11, 13, 12, 0),
    gsSP1Triangle(14, 15, 16, 0),
    gsSP1Triangle(17, 18, 19, 0),
    gsSPVertex(&sConnectedPineVertices[20], 20, 0),
    gsSP2Triangles(0, 1, 2, 0, 1, 3, 2, 0),
    gsSP1Triangle(4, 5, 6, 0),
    gsSP1Triangle(7, 8, 9, 0),
    gsSP2Triangles(10, 11, 12, 0, 11, 13, 12, 0),
    gsSP1Triangle(14, 15, 16, 0),
    gsSP1Triangle(17, 18, 19, 0),
    gsSPVertex(sConnectedLandmarkVertices, 9, 0),
    gsSP2Triangles(0, 1, 4, 0, 1, 5, 4, 0),
    gsSP2Triangles(1, 3, 5, 0, 3, 7, 5, 0),
    gsSP2Triangles(3, 2, 7, 0, 2, 6, 7, 0),
    gsSP2Triangles(2, 0, 6, 0, 0, 4, 6, 0),
    gsSP2Triangles(4, 5, 8, 0, 5, 7, 8, 0),
    gsSP2Triangles(7, 6, 8, 0, 6, 4, 8, 0),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPPipeSync(),
    gsSPEndDisplayList(),
};
