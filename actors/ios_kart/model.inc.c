/* Original low-poly kart geometry for the iOS gameplay prototype.
 * No Mario Kart 64 or extracted ROM geometry is used. */
#define IOS_KART_VERTEX(x, y, z, r, g, b) {{{x, y, z}, 0, {0, 0}, {r, g, b, 255}}}

#define IOS_KART_BOX(name, x0, x1, y0, y1, z0, z1, r, g, b) \
static const Vtx name##_vertices[8] = { \
    IOS_KART_VERTEX(x0, y0, z0, r, g, b), IOS_KART_VERTEX(x1, y0, z0, r, g, b), \
    IOS_KART_VERTEX(x0, y1, z0, r, g, b), IOS_KART_VERTEX(x1, y1, z0, r, g, b), \
    IOS_KART_VERTEX(x0, y0, z1, r, g, b), IOS_KART_VERTEX(x1, y0, z1, r, g, b), \
    IOS_KART_VERTEX(x0, y1, z1, r, g, b), IOS_KART_VERTEX(x1, y1, z1, r, g, b), \
}; \
static const Gfx name[] = { \
    gsSPVertex(name##_vertices, 8, 0), \
    gsSP2Triangles(4, 5, 7, 0, 4, 7, 6, 0), \
    gsSP2Triangles(2, 3, 7, 0, 2, 7, 6, 0), \
    gsSP2Triangles(0, 2, 3, 0, 0, 3, 1, 0), \
    gsSP2Triangles(0, 1, 5, 0, 0, 5, 4, 0), \
    gsSP2Triangles(0, 4, 6, 0, 0, 6, 2, 0), \
    gsSP2Triangles(1, 3, 7, 0, 1, 7, 5, 0), \
    gsSPEndDisplayList(), \
}

/* Wedge-shaped main tub: narrower at the nose and raised around the cockpit. */
static const Vtx ios_kart_chassis_vertices[8] = {
    IOS_KART_VERTEX(-43, 14, -62, 148, 22, 31), IOS_KART_VERTEX(43, 14, -62, 148, 22, 31),
    IOS_KART_VERTEX(-34, 43, -50, 224, 44, 49), IOS_KART_VERTEX(34, 43, -50, 224, 44, 49),
    IOS_KART_VERTEX(-43, 14, 55, 148, 22, 31), IOS_KART_VERTEX(43, 14, 55, 148, 22, 31),
    IOS_KART_VERTEX(-28, 38, 67, 236, 48, 44), IOS_KART_VERTEX(28, 38, 67, 236, 48, 44),
};

static const Gfx ios_kart_chassis_dl[] = {
    gsSPVertex(ios_kart_chassis_vertices, 8, 0),
    gsSP2Triangles(4, 5, 7, 0, 4, 7, 6, 0),
    gsSP2Triangles(0, 2, 3, 0, 0, 3, 1, 0),
    gsSP2Triangles(0, 4, 6, 0, 0, 6, 2, 0),
    gsSP2Triangles(1, 3, 7, 0, 1, 7, 5, 0),
    gsSP2Triangles(2, 6, 7, 0, 2, 7, 3, 0),
    gsSP2Triangles(0, 1, 5, 0, 0, 5, 4, 0),
    gsSPEndDisplayList(),
};

/* A sculpted bonnet tip adds a visible front end even from the chase camera. */
static const Vtx ios_kart_nose_vertices[6] = {
    IOS_KART_VERTEX(-28, 20, 56, 238, 175, 35), IOS_KART_VERTEX(28, 20, 56, 238, 175, 35),
    IOS_KART_VERTEX(-22, 39, 64, 255, 211, 69), IOS_KART_VERTEX(22, 39, 64, 255, 211, 69),
    IOS_KART_VERTEX(0, 27, 96, 255, 224, 92), IOS_KART_VERTEX(0, 39, 80, 255, 238, 128),
};
static const Gfx ios_kart_nose_dl[] = {
    gsSPVertex(ios_kart_nose_vertices, 6, 0),
    gsSP2Triangles(0, 1, 3, 0, 0, 3, 2, 0),
    gsSP2Triangles(2, 3, 5, 0, 0, 4, 1, 0),
    gsSP2Triangles(0, 2, 5, 0, 1, 4, 3, 0),
    gsSP2Triangles(3, 4, 5, 0, 0, 5, 4, 0),
    gsSPEndDisplayList(),
};

/* Cylinder along local X.  Each wheel is a separate geo node, so spin and
 * front-wheel steering are composed as transforms without rewriting vertices. */
static const Vtx ios_kart_tire_vertices[18] = {
    IOS_KART_VERTEX(-8, 18, 0, 30, 31, 35), IOS_KART_VERTEX(-8, 13, 13, 30, 31, 35),
    IOS_KART_VERTEX(-8, 0, 18, 30, 31, 35), IOS_KART_VERTEX(-8, -13, 13, 30, 31, 35),
    IOS_KART_VERTEX(-8, -18, 0, 30, 31, 35), IOS_KART_VERTEX(-8, -13, -13, 30, 31, 35),
    IOS_KART_VERTEX(-8, 0, -18, 30, 31, 35), IOS_KART_VERTEX(-8, 13, -13, 30, 31, 35),
    IOS_KART_VERTEX(8, 18, 0, 30, 31, 35), IOS_KART_VERTEX(8, 13, 13, 30, 31, 35),
    IOS_KART_VERTEX(8, 0, 18, 30, 31, 35), IOS_KART_VERTEX(8, -13, 13, 30, 31, 35),
    IOS_KART_VERTEX(8, -18, 0, 30, 31, 35), IOS_KART_VERTEX(8, -13, -13, 30, 31, 35),
    IOS_KART_VERTEX(8, 0, -18, 30, 31, 35), IOS_KART_VERTEX(8, 13, -13, 30, 31, 35),
    IOS_KART_VERTEX(-8, 0, 0, 30, 31, 35), IOS_KART_VERTEX(8, 0, 0, 30, 31, 35),
};
static const Gfx ios_kart_tire_dl[] = {
    gsSPVertex(ios_kart_tire_vertices, 18, 0),
    gsSP2Triangles(0, 1, 9, 0, 0, 9, 8, 0),
    gsSP2Triangles(1, 2, 10, 0, 1, 10, 9, 0),
    gsSP2Triangles(2, 3, 11, 0, 2, 11, 10, 0),
    gsSP2Triangles(3, 4, 12, 0, 3, 12, 11, 0),
    gsSP2Triangles(4, 5, 13, 0, 4, 13, 12, 0),
    gsSP2Triangles(5, 6, 14, 0, 5, 14, 13, 0),
    gsSP2Triangles(6, 7, 15, 0, 6, 15, 14, 0),
    gsSP2Triangles(7, 0, 8, 0, 7, 8, 15, 0),
    gsSP2Triangles(16, 1, 0, 0, 16, 2, 1, 0),
    gsSP2Triangles(16, 3, 2, 0, 16, 4, 3, 0),
    gsSP2Triangles(16, 5, 4, 0, 16, 6, 5, 0),
    gsSP2Triangles(16, 7, 6, 0, 16, 0, 7, 0),
    gsSP2Triangles(17, 8, 9, 0, 17, 9, 10, 0),
    gsSP2Triangles(17, 10, 11, 0, 17, 11, 12, 0),
    gsSP2Triangles(17, 12, 13, 0, 17, 13, 14, 0),
    gsSP2Triangles(17, 14, 15, 0, 17, 15, 8, 0),
    gsSPEndDisplayList(),
};

static const Vtx ios_kart_hub_vertices[18] = {
    IOS_KART_VERTEX(-9, 9, 0, 150, 164, 177), IOS_KART_VERTEX(-9, 6, 6, 150, 164, 177),
    IOS_KART_VERTEX(-9, 0, 9, 150, 164, 177), IOS_KART_VERTEX(-9, -6, 6, 150, 164, 177),
    IOS_KART_VERTEX(-9, -9, 0, 150, 164, 177), IOS_KART_VERTEX(-9, -6, -6, 150, 164, 177),
    IOS_KART_VERTEX(-9, 0, -9, 150, 164, 177), IOS_KART_VERTEX(-9, 6, -6, 150, 164, 177),
    IOS_KART_VERTEX(9, 9, 0, 150, 164, 177), IOS_KART_VERTEX(9, 6, 6, 150, 164, 177),
    IOS_KART_VERTEX(9, 0, 9, 150, 164, 177), IOS_KART_VERTEX(9, -6, 6, 150, 164, 177),
    IOS_KART_VERTEX(9, -9, 0, 150, 164, 177), IOS_KART_VERTEX(9, -6, -6, 150, 164, 177),
    IOS_KART_VERTEX(9, 0, -9, 150, 164, 177), IOS_KART_VERTEX(9, 6, -6, 150, 164, 177),
    IOS_KART_VERTEX(-9, 0, 0, 150, 164, 177), IOS_KART_VERTEX(9, 0, 0, 150, 164, 177),
};
static const Gfx ios_kart_hub_dl[] = {
    gsSPVertex(ios_kart_hub_vertices, 18, 0),
    gsSP2Triangles(0, 1, 9, 0, 0, 9, 8, 0),
    gsSP2Triangles(1, 2, 10, 0, 1, 10, 9, 0),
    gsSP2Triangles(2, 3, 11, 0, 2, 11, 10, 0),
    gsSP2Triangles(3, 4, 12, 0, 3, 12, 11, 0),
    gsSP2Triangles(4, 5, 13, 0, 4, 13, 12, 0),
    gsSP2Triangles(5, 6, 14, 0, 5, 14, 13, 0),
    gsSP2Triangles(6, 7, 15, 0, 6, 15, 14, 0),
    gsSP2Triangles(7, 0, 8, 0, 7, 8, 15, 0),
    gsSP2Triangles(16, 1, 0, 0, 16, 2, 1, 0),
    gsSP2Triangles(16, 3, 2, 0, 16, 4, 3, 0),
    gsSP2Triangles(16, 5, 4, 0, 16, 6, 5, 0),
    gsSP2Triangles(16, 7, 6, 0, 16, 0, 7, 0),
    gsSP2Triangles(17, 8, 9, 0, 17, 9, 10, 0),
    gsSP2Triangles(17, 10, 11, 0, 17, 11, 12, 0),
    gsSP2Triangles(17, 12, 13, 0, 17, 13, 14, 0),
    gsSP2Triangles(17, 14, 15, 0, 17, 15, 8, 0),
    gsSPEndDisplayList(),
};

IOS_KART_BOX(ios_kart_floor,       -39, 39, 10, 19, -56, 58, 65, 75, 83);
IOS_KART_BOX(ios_kart_left_sidepod, -48, -32, 20, 39, -40, 38, 192, 32, 42);
IOS_KART_BOX(ios_kart_right_sidepod, 32, 48, 20, 39, -40, 38, 192, 32, 42);
IOS_KART_BOX(ios_kart_seat_cushion, -23, 23, 43, 51, -29, 14, 38, 69, 112);
IOS_KART_BOX(ios_kart_seat_back,    -24, 24, 48, 82, -35, -23, 31, 49, 83);
IOS_KART_BOX(ios_kart_engine,       -22, 22, 34, 58, -59, -37, 65, 103, 61);
IOS_KART_BOX(ios_kart_front_axle,   -57, 57, 13, 18, 39, 47, 115, 123, 132);
IOS_KART_BOX(ios_kart_rear_axle,    -57, 57, 13, 18, -49, -41, 115, 123, 132);
IOS_KART_BOX(ios_kart_front_bumper, -55, 55, 13, 25, 78, 88, 191, 199, 204);
IOS_KART_BOX(ios_kart_rear_bumper,  -55, 55, 13, 25, -75, -65, 191, 199, 204);
IOS_KART_BOX(ios_kart_steer_column,  -4, 4, 49, 78, 31, 39, 75, 82, 91);
IOS_KART_BOX(ios_kart_wheel_top,    -20, 20, 79, 84, 40, 47, 38, 42, 48);
IOS_KART_BOX(ios_kart_wheel_left,   -20, -15, 64, 84, 40, 47, 38, 42, 48);
IOS_KART_BOX(ios_kart_wheel_right,   15, 20, 64, 84, 40, 47, 38, 42, 48);
IOS_KART_BOX(ios_kart_exhaust_left, -30, -24, 43, 49, -73, -52, 163, 170, 177);
IOS_KART_BOX(ios_kart_exhaust_right, 24, 30, 43, 49, -73, -52, 163, 170, 177);

const Gfx ios_kart_wheel_dl[] = {
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPDisplayList(ios_kart_tire_dl),
    gsSPDisplayList(ios_kart_hub_dl),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPEndDisplayList(),
};

const Gfx ios_kart_display_list[] = {
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPDisplayList(ios_kart_floor),
    gsSPDisplayList(ios_kart_chassis_dl),
    gsSPDisplayList(ios_kart_nose_dl),
    gsSPDisplayList(ios_kart_left_sidepod),
    gsSPDisplayList(ios_kart_right_sidepod),
    gsSPDisplayList(ios_kart_seat_cushion),
    gsSPDisplayList(ios_kart_seat_back),
    gsSPDisplayList(ios_kart_engine),
    gsSPDisplayList(ios_kart_front_axle),
    gsSPDisplayList(ios_kart_rear_axle),
    gsSPDisplayList(ios_kart_front_bumper),
    gsSPDisplayList(ios_kart_rear_bumper),
    gsSPDisplayList(ios_kart_steer_column),
    gsSPDisplayList(ios_kart_wheel_top),
    gsSPDisplayList(ios_kart_wheel_left),
    gsSPDisplayList(ios_kart_wheel_right),
    gsSPDisplayList(ios_kart_exhaust_left),
    gsSPDisplayList(ios_kart_exhaust_right),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPEndDisplayList(),
};

#undef IOS_KART_BOX
#undef IOS_KART_VERTEX
