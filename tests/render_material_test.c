#include "engine/render_material.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    const enum GfxRenderMaterialClass classes[] = {
        GFX_RENDER_MATERIAL_UNCLASSIFIED,
        GFX_RENDER_MATERIAL_WORLD,
        GFX_RENDER_MATERIAL_OBJECT,
        GFX_RENDER_MATERIAL_CHARACTER,
    };
    for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i) {
        uint32_t tag = gfx_render_material_tag(classes[i]);
        assert((tag & GFX_RENDER_MATERIAL_TAG_MASK) == GFX_RENDER_MATERIAL_TAG_BASE);
        assert(gfx_render_material_class_from_tag(tag) == (int)classes[i]);
    }
    assert(gfx_render_material_class_from_tag(0xd6730201u) == -1);
    assert(gfx_render_material_class_from_tag(GFX_RENDER_MATERIAL_TAG_BASE | 4u) == -1);
    puts("Fast3D material class tag encoding and validation passed");
    return 0;
}
