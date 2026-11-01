/* Flat, animated portal aperture fallback. This is an ordinary SM64 display
 * list rendered through the active graphics backend; the center is tinted but
 * does not yet show a linked camera view. */
#define PORTAL_VERT(x, y, z, r, g, b, a) {{{x, y, z}, 0, {0, 0}, {r, g, b, a}}}

static const Vtx portal_blue_vertices[] = {
    PORTAL_VERT(-78, 180, 0,  40, 170, 255, 255), PORTAL_VERT( 78, 180, 0,  40, 170, 255, 255),
    PORTAL_VERT( 58, 150, 0,  40, 170, 255, 255), PORTAL_VERT(-58, 150, 0,  40, 170, 255, 255),
    PORTAL_VERT( 78, 180, 0,  40, 170, 255, 255), PORTAL_VERT( 78,-180, 0,  40, 170, 255, 255),
    PORTAL_VERT( 58,-150, 0,  40, 170, 255, 255), PORTAL_VERT( 58, 150, 0,  40, 170, 255, 255),
    PORTAL_VERT(-78,-180, 0,  40, 170, 255, 255), PORTAL_VERT( 78,-180, 0,  40, 170, 255, 255),
    PORTAL_VERT( 58,-150, 0,  40, 170, 255, 255), PORTAL_VERT(-58,-150, 0,  40, 170, 255, 255),
    PORTAL_VERT(-78, 180, 0,  40, 170, 255, 255), PORTAL_VERT(-78,-180, 0,  40, 170, 255, 255),
    PORTAL_VERT(-58,-150, 0,  40, 170, 255, 255), PORTAL_VERT(-58, 150, 0,  40, 170, 255, 255),
    PORTAL_VERT(-58, 150, 1,   8,  35, 100, 180), PORTAL_VERT( 58, 150, 1,   8,  35, 100, 180),
    PORTAL_VERT( 58,-150, 1,   8,  35, 100, 180), PORTAL_VERT(-58,-150, 1,   8,  35, 100, 180),
};

static const Vtx portal_orange_vertices[] = {
    PORTAL_VERT(-78, 180, 0, 255, 120,  24, 255), PORTAL_VERT( 78, 180, 0, 255, 120,  24, 255),
    PORTAL_VERT( 58, 150, 0, 255, 120,  24, 255), PORTAL_VERT(-58, 150, 0, 255, 120,  24, 255),
    PORTAL_VERT( 78, 180, 0, 255, 120,  24, 255), PORTAL_VERT( 78,-180, 0, 255, 120,  24, 255),
    PORTAL_VERT( 58,-150, 0, 255, 120,  24, 255), PORTAL_VERT( 58, 150, 0, 255, 120,  24, 255),
    PORTAL_VERT(-78,-180, 0, 255, 120,  24, 255), PORTAL_VERT( 78,-180, 0, 255, 120,  24, 255),
    PORTAL_VERT( 58,-150, 0, 255, 120,  24, 255), PORTAL_VERT(-58,-150, 0, 255, 120,  24, 255),
    PORTAL_VERT(-78, 180, 0, 255, 120,  24, 255), PORTAL_VERT(-78,-180, 0, 255, 120,  24, 255),
    PORTAL_VERT(-58,-150, 0, 255, 120,  24, 255), PORTAL_VERT(-58, 150, 0, 255, 120,  24, 255),
    PORTAL_VERT(-58, 150, 1, 100,  28,   4, 180), PORTAL_VERT( 58, 150, 1, 100,  28,   4, 180),
    PORTAL_VERT( 58,-150, 1, 100,  28,   4, 180), PORTAL_VERT(-58,-150, 1, 100,  28,   4, 180),
};

#define PORTAL_DISPLAY_LIST(name, vertices) \
static const Gfx name[] = { \
    gsDPPipeSync(), \
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE), \
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK), \
    gsSPVertex(vertices, 20, 0), \
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0), \
    gsSP2Triangles(4, 5, 6, 0, 4, 6, 7, 0), \
    gsSP2Triangles(8, 9, 10, 0, 8, 10, 11, 0), \
    gsSP2Triangles(12, 13, 14, 0, 12, 14, 15, 0), \
    gsSP2Triangles(16, 17, 18, 0, 16, 18, 19, 0), \
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK), \
    gsSPEndDisplayList(), \
}

PORTAL_DISPLAY_LIST(portal_blue_dl, portal_blue_vertices);
PORTAL_DISPLAY_LIST(portal_orange_dl, portal_orange_vertices);

const GeoLayout portal_blue_geo[] = {
    GEO_CULLING_RADIUS(500),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_TRANSPARENT, portal_blue_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};

const GeoLayout portal_orange_geo[] = {
    GEO_CULLING_RADIUS(500),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_TRANSPARENT, portal_orange_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};

#undef PORTAL_DISPLAY_LIST
#undef PORTAL_VERT
