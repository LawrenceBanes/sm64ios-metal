/* Original untextured placeholder go-kart authored for the iOS gameplay test.
 * It uses no Mario Kart 64 models, textures, or extracted assets. */
#define IOS_KART_VERTEX(x, y, z, r, g, b) {{{x, y, z}, 0, {0, 0}, {r, g, b, 255}}}

#define IOS_KART_CUBE(name, x0, x1, y0, y1, z0, z1, r, g, b) \
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

IOS_KART_CUBE(ios_kart_body_dl,   -44, 44, 18, 50, -66, 62, 210, 38, 34);
IOS_KART_CUBE(ios_kart_nose_dl,   -32, 32, 24, 54,  48, 88, 245, 190, 40);
IOS_KART_CUBE(ios_kart_seat_dl,   -24, 24, 48, 70, -22, 20,  38, 72, 130);
IOS_KART_CUBE(ios_kart_front_bumper_dl, -50, 50, 12, 26, 82, 94, 185, 190, 195);
IOS_KART_CUBE(ios_kart_rear_bumper_dl,  -50, 50, 12, 26, -82, -70, 185, 190, 195);
IOS_KART_CUBE(ios_kart_left_front_wheel_dl,  -60, -42, 0, 30,  38, 58, 35, 36, 40);
IOS_KART_CUBE(ios_kart_right_front_wheel_dl, 42, 60, 0, 30,  38, 58, 35, 36, 40);
IOS_KART_CUBE(ios_kart_left_rear_wheel_dl,  -60, -42, 0, 30, -56, -36, 35, 36, 40);
IOS_KART_CUBE(ios_kart_right_rear_wheel_dl, 42, 60, 0, 30, -56, -36, 35, 36, 40);

const Gfx ios_kart_display_list[] = {
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPDisplayList(ios_kart_body_dl),
    gsSPDisplayList(ios_kart_nose_dl),
    gsSPDisplayList(ios_kart_seat_dl),
    gsSPDisplayList(ios_kart_front_bumper_dl),
    gsSPDisplayList(ios_kart_rear_bumper_dl),
    gsSPDisplayList(ios_kart_left_front_wheel_dl),
    gsSPDisplayList(ios_kart_right_front_wheel_dl),
    gsSPDisplayList(ios_kart_left_rear_wheel_dl),
    gsSPDisplayList(ios_kart_right_rear_wheel_dl),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPEndDisplayList(),
};

#undef IOS_KART_CUBE
#undef IOS_KART_VERTEX
