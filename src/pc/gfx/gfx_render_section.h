#ifndef GFX_RENDER_SECTION_H
#define GFX_RENDER_SECTION_H

/* Tagged display-list no-ops. They are interpreted only by the diagnostic
 * renderer and leave ordinary display-list state and batching untouched. */
#define GFX_RENDER_SECTION_TAG_MASK 0xffff0000u
#define GFX_RENDER_SECTION_BEGIN_TAG 0xd6710000u
#define GFX_RENDER_SECTION_END_TAG 0xd6720000u
#define GFX_RENDER_UI_OVERLAY_BEGIN_TAG 0xd6730000u
#define GFX_RENDER_SECTION_BEGIN(id) gsDPNoOpTag(GFX_RENDER_SECTION_BEGIN_TAG | (id))
#define GFX_RENDER_SECTION_END(id) gsDPNoOpTag(GFX_RENDER_SECTION_END_TAG | (id))

enum GfxRenderSectionId {
    GFX_RENDER_HYRULE_OPAQUE = 1,
    GFX_RENDER_HYRULE_TRANSLUCENT = 2,
    GFX_RENDER_SCENE_OPAQUE = 4,
    GFX_RENDER_SCENE_TRANSLUCENT = 5,
};

#endif
