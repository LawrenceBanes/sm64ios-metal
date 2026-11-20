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
    float object_transform[4][4];
    /* False when the frame camera could not be separated unambiguously. */
    bool has_world_transform;
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
    float view_matrix[4][4];
    float projection_matrix[4][4];
    bool has_view_matrix;
    bool has_projection_matrix;
};

void gfx_scene_capture_set_enabled(bool enabled);
bool gfx_scene_capture_is_enabled(void);
void gfx_scene_capture_begin_frame(void);
/* Root-camera updates before the first triangle are accepted. A later camera
 * change invalidates world transforms for the whole single-camera frame. */
void gfx_scene_capture_set_view_matrix(const float matrix[4][4]);
void gfx_scene_capture_set_projection_matrix(const float matrix[4][4]);
bool gfx_scene_capture_split_modelview(const float modelview[4][4], const float view[4][4], float object_transform[4][4]);
bool gfx_scene_capture_submit(const struct GfxSceneCaptureTriangle *triangle);
struct GfxSceneCaptureFrame gfx_scene_capture_get_frame(void);
/* Bound capture allocation when an optional analysis feature is enabled.
 * Zero restores the production hard maximum. */
void gfx_scene_capture_set_triangle_limit(size_t maximum);
uint64_t gfx_scene_capture_hash_combine(uint64_t parent, uintptr_t source);
uint64_t gfx_scene_capture_hash_bytes(uint64_t parent, const void *data, size_t size);

#endif
