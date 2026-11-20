#ifndef GFX_RT_SCENE_H
#define GFX_RT_SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_scene_capture.h"

#define GFX_RT_SCENE_MAX_TRIANGLES 16384u
#define GFX_RT_SCENE_MAX_CAPTURE_INPUT 65536u

struct GfxRTTriangle {
    float position[3][3];
};

struct GfxRTScene {
    const struct GfxRTTriangle *triangles;
    size_t triangle_count;
    size_t dropped_triangles;
    uint64_t geometry_hash;
    uint64_t source_frame;
    float view_matrix[4][4];
    float projection_matrix[4][4];
    bool has_camera;
};

/* Keep only world-space instances whose geometry and transform were also
 * present in the immediately preceding frame. Moving actors therefore don't
 * churn the static-scene acceleration structure. */
struct GfxRTScene gfx_rt_scene_capture_static(const struct GfxSceneCaptureFrame *frame);
void gfx_rt_scene_reset(void);

#endif
