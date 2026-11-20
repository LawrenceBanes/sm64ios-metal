#ifndef ENGINE_RENDER_MATERIAL_H
#define ENGINE_RENDER_MATERIAL_H

#include <stdint.h>

/* Small object category contract shared by GeoLayout tagging, the Fast3D
 * interpreter, and renderer backends. Zero is deliberately unclassified. */
enum GfxRenderMaterialClass {
    GFX_RENDER_MATERIAL_UNCLASSIFIED = 0,
    GFX_RENDER_MATERIAL_WORLD = 1,
    GFX_RENDER_MATERIAL_OBJECT = 2,
    GFX_RENDER_MATERIAL_CHARACTER = 3,
};

#define GFX_RENDER_MATERIAL_TAG_BASE 0xd6730100u
#define GFX_RENDER_MATERIAL_TAG_MASK 0xfffffffcu

static inline uint32_t gfx_render_material_tag(enum GfxRenderMaterialClass material_class) {
    return GFX_RENDER_MATERIAL_TAG_BASE | ((uint32_t)material_class & 3u);
}

static inline int gfx_render_material_class_from_tag(uint32_t tag) {
    if ((tag & GFX_RENDER_MATERIAL_TAG_MASK) != GFX_RENDER_MATERIAL_TAG_BASE) return -1;
    return (int)(tag & 3u);
}

#endif
