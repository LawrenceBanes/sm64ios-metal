#include "pc/gfx/gfx_scene_bvh.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set_triangle(struct GfxBvhTriangle *triangle, float center_x, float z,
                         uint64_t instance_id, uint32_t material_id) {
    memset(triangle, 0, sizeof(*triangle));
    triangle->v0[0] = center_x - 0.4f; triangle->v0[1] = -0.4f; triangle->v0[2] = z;
    triangle->v1[0] = center_x + 0.4f; triangle->v1[1] = -0.4f; triangle->v1[2] = z;
    triangle->v2[0] = center_x;        triangle->v2[1] =  0.4f; triangle->v2[2] = z;
    triangle->instance_id = instance_id;
    triangle->material_id = material_id;
}

int main(void) {
    struct GfxBvhTriangle triangles[7];
    for (int i = 0; i < 6; ++i)
        set_triangle(&triangles[i], (float)i - 3.0f, 5.0f, (uint64_t)(100 + i), (uint32_t)(20 + i));
    set_triangle(&triangles[6], 0.0f, 2.0f, 900, 77);

    struct GfxSceneBvh bvh = {0};
    assert(gfx_scene_bvh_build(&bvh, triangles, 7, 48));
    assert(bvh.node_count > 1);
    struct GfxBvhRay ray = {{0, 0, 0}, {0, 0, 1}, 0.001f, 100.0f};
    struct GfxBvhHit hit;
    assert(gfx_scene_bvh_trace(&bvh, &ray, &hit) == GFX_BVH_TRACE_HIT);
    assert(hit.triangle_index == 6);
    assert(hit.instance_id == 900 && hit.material_id == 77);
    assert(hit.distance > 1.99f && hit.distance < 2.01f);
    assert(hit.barycentric_u >= 0.0f && hit.barycentric_v >= 0.0f);

    ray.origin[0] = 20.0f;
    assert(gfx_scene_bvh_trace(&bvh, &ray, &hit) == GFX_BVH_TRACE_MISS);
    ray.origin[0] = 0.0f;
    ray.t_max = 1.0f;
    assert(gfx_scene_bvh_trace(&bvh, &ray, &hit) == GFX_BVH_TRACE_MISS);

    assert(!gfx_scene_bvh_build(&bvh, triangles, 7, 0));
    assert(bvh.nodes != NULL && bvh.triangle_indices != NULL && bvh.node_count > 0);
    ray.t_max = 100.0f;
    assert(gfx_scene_bvh_trace(&bvh, &ray, &hit) == GFX_BVH_TRACE_HIT);
    assert(gfx_scene_bvh_build(&bvh, NULL, 0, 48));
    assert(gfx_scene_bvh_trace(&bvh, &ray, &hit) == GFX_BVH_TRACE_MISS);

    struct GfxBvhTriangle many[128];
    for (int i = 0; i < 128; ++i)
        set_triangle(&many[i], (float)i * 2.0f, 10.0f + (float)(i % 5), (uint64_t)i, (uint32_t)i);
    assert(gfx_scene_bvh_build(&bvh, many, 128, 48));
    for (int i = 0; i < 128; ++i) {
        ray.origin[0] = (float)i * 2.0f;
        ray.t_max = 100.0f;
        assert(gfx_scene_bvh_trace(&bvh, &ray, &hit) == GFX_BVH_TRACE_HIT);
        assert(hit.triangle_index == (uint32_t)i);
        assert(hit.instance_id == (uint64_t)i && hit.material_id == (uint32_t)i);
    }
    gfx_scene_bvh_destroy(&bvh);
    assert(bvh.nodes == NULL && bvh.triangle_indices == NULL && bvh.triangles == NULL);
    puts("CPU scene BVH build, nearest-hit, miss, bounds, and lifecycle checks passed");
    return 0;
}
