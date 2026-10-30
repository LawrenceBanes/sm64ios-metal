#ifndef GFX_SCENE_BVH_H
#define GFX_SCENE_BVH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Triangle vertices must already be in the same world-space coordinate system. */
struct GfxBvhTriangle {
    float v0[3];
    float v1[3];
    float v2[3];
    uint64_t instance_id;
    uint32_t material_id;
};

struct GfxBvhNode {
    float bounds_min[3];
    float bounds_max[3];
    uint32_t left;
    uint32_t right;
    uint32_t first;
    uint32_t count;
};

struct GfxSceneBvh {
    const struct GfxBvhTriangle *triangles; /* borrowed; valid until destroy/rebuild */
    struct GfxBvhNode *nodes;
    uint32_t *triangle_indices;
    size_t triangle_count;
    size_t node_count;
    size_t node_capacity;
    uint32_t max_depth;
};

struct GfxBvhRay {
    float origin[3];
    float direction[3];
    float t_min;
    float t_max;
};

struct GfxBvhHit {
    uint32_t triangle_index;
    uint64_t instance_id;
    uint32_t material_id;
    float distance;
    float barycentric_u;
    float barycentric_v;
};

enum GfxBvhTraceResult {
    GFX_BVH_TRACE_MISS = 0,
    GFX_BVH_TRACE_HIT = 1,
    GFX_BVH_TRACE_STACK_OVERFLOW = 2,
};

void gfx_scene_bvh_destroy(struct GfxSceneBvh *bvh);
bool gfx_scene_bvh_build(struct GfxSceneBvh *bvh,
                         const struct GfxBvhTriangle *triangles, size_t triangle_count,
                         uint32_t max_depth);
enum GfxBvhTraceResult gfx_scene_bvh_trace(const struct GfxSceneBvh *bvh,
                                           const struct GfxBvhRay *ray,
                                           struct GfxBvhHit *hit);

#endif
