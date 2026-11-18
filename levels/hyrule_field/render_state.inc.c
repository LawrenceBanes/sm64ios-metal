/* The imported room expects the scene to supply light and fog state. Give
 * the static field a neutral light in both areas. Its animated environment
 * is not imported, so make the room's fog blend contribute zero. */
static const Lights1 hyrule_field_light = gdSPDefLights1(
    0x70, 0x70, 0x70, 0x90, 0x90, 0x90, 40, 100, 40);

static const Gfx hyrule_field_begin_dl[] = {
    gsDPPipeSync(),
    gsSPSetLights1(hyrule_field_light),
    gsSPFogFactor(0, 0),
    gsSPClearGeometryMode(G_CULL_FRONT | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR),
    gsSPSetGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK),
    gsDPSetEnvColor(255, 255, 255, 255),
    gsSPEndDisplayList(),
};

/* SM64 submits other objects on these same layers. In particular G_FOG
 * replaces vertex alpha in gfx_sp_vertex even on a subsequent non-fog draw. */
static const Gfx hyrule_field_end_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_FRONT),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPTexture(0xffff, 0xffff, 0, G_TX_RENDERTILE, G_OFF),
    gsDPSetTextureLUT(G_TT_NONE),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
    gsSPEndDisplayList(),
};
