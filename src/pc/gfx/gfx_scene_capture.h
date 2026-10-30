#ifndef GFX_SCENE_CAPTURE_H
#define GFX_SCENE_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct GfxSceneCaptureVertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
};

struct GfxSceneCaptureTriangle {
    struct GfxSceneCaptureVertex vertices[3];
    /* RSP model-view includes camera state on some display lists; not world space yet. */
    float modelview[4][4];
    float projection[4][4];
    uint64_t geometry_id;
    uint64_t instance_id;
    uint32_t combiner_id;
    uint64_t texture_id[2];
    uint32_t sampler_state[2];
    uint32_t material_flags;
};

struct GfxSceneCaptureFrame {
    const struct GfxSceneCaptureTriangle *triangles;
    size_t triangle_count;
    size_t dropped_triangles;
    uint64_t frame_index;
};

void gfx_scene_capture_set_enabled(bool enabled);
bool gfx_scene_capture_is_enabled(void);
void gfx_scene_capture_begin_frame(void);
bool gfx_scene_capture_submit(const struct GfxSceneCaptureTriangle *triangle);
struct GfxSceneCaptureFrame gfx_scene_capture_get_frame(void);
uint64_t gfx_scene_capture_hash_combine(uint64_t parent, uintptr_t source);

#endif
