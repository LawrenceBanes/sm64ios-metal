#include "pc/gfx/gfx_shadow_map.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    const float object_transform[4][4] = {
        { 2.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f, 0.0f }, { 7.0f, -5.0f, 11.0f, 1.0f }
    };
    const float local_position[3] = { 2.0f, 3.0f, -1.0f };
    float world_position[3];
    assert(gfx_shadow_map_transform_point(object_transform, local_position, world_position));
    assert(world_position[0] == 11.0f && world_position[1] == 4.0f && world_position[2] == 7.0f);
    const float invalid_transform[4][4] = {
        { 1, 0, 0, 0 }, { 0, NAN, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };
    assert(!gfx_shadow_map_transform_point(invalid_transform, local_position, world_position));
    const float vertices[][3] = {
        { -100.0f, 0.0f, -100.0f }, { 100.0f, 0.0f, -100.0f },
        { 100.0f, 0.0f, 100.0f }, { -100.0f, 0.0f, 100.0f },
        { 0.0f, 180.0f, 0.0f }
    };
    const float sun_ray[3] = { -0.35f, -1.0f, -0.25f };
    struct GfxShadowMapTransform transform;
    assert(gfx_shadow_map_fit(vertices, 5, sun_ray, 1024, &transform));
    for (size_t i = 0; i < 5; ++i) {
        float clip[3], uv_depth[3];
        assert(gfx_shadow_map_project(&transform, vertices[i], clip, uv_depth));
        assert(uv_depth[0] >= 0.0f && uv_depth[0] <= 1.0f);
        assert(uv_depth[1] >= 0.0f && uv_depth[1] <= 1.0f);
        assert(uv_depth[2] >= 0.0f && uv_depth[2] <= 1.0f);
        assert(fabsf(clip[0]) <= 1.0f && fabsf(clip[1]) <= 1.0f);
    }
    const float asymmetric[][3] = {
        { -310.0f, 12.0f, -40.0f }, { 71.0f, 16.0f, -35.0f },
        { 34.0f, 18.0f, 95.0f }, { -289.0f, 11.0f, 72.0f }
    };
    struct GfxShadowMapTransform asymmetric_transform;
    assert(gfx_shadow_map_fit(asymmetric, 4, sun_ray, 512, &asymmetric_transform));
    for (size_t i = 0; i < 4; ++i) {
        float clip[3], uv_depth[3];
        assert(gfx_shadow_map_project(&asymmetric_transform, asymmetric[i], clip, uv_depth));
        assert(fabsf(clip[0]) <= 1.00001f && fabsf(clip[1]) <= 1.00001f);
    }
    struct GfxShadowMapTransform shifted;
    const float shifted_vertices[][3] = {
        { -100.0f + 0.02f, 0.0f, -100.0f }, { 100.0f + 0.02f, 0.0f, -100.0f },
        { 100.0f + 0.02f, 0.0f, 100.0f }, { -100.0f + 0.02f, 0.0f, 100.0f },
        { 0.0f + 0.02f, 180.0f, 0.0f }
    };
    assert(gfx_shadow_map_fit(shifted_vertices, 5, sun_ray, 1024, &shifted));
    assert(fabsf(transform.minimum[0] - shifted.minimum[0]) < 1.0e-6f);
    assert(fabsf(transform.minimum[1] - shifted.minimum[1]) < 1.0e-6f);
    assert(!gfx_shadow_map_fit(vertices, 0, sun_ray, 1024, &transform));
    assert(!gfx_shadow_map_fit(vertices, 5, (float[3]){ 0, 0, 0 }, 1024, &transform));
    assert(!gfx_shadow_map_fit(vertices, 5, sun_ray, 8, &transform));
    assert(gfx_shadow_map_geometry_key(vertices, 5, sun_ray, 1024) ==
           gfx_shadow_map_geometry_key(vertices, 5, sun_ray, 1024));
    assert(gfx_shadow_map_geometry_key(vertices, 5, sun_ray, 1024) !=
           gfx_shadow_map_geometry_key(shifted_vertices, 5, sun_ray, 1024));
    assert(gfx_shadow_map_geometry_key(vertices, 5, sun_ray, 1024) !=
           gfx_shadow_map_geometry_key(vertices, 5, (float[3]){ -0.34f, -1.0f, -0.25f }, 1024));
    assert(gfx_shadow_map_geometry_key(vertices, 5, sun_ray, 1024) !=
           gfx_shadow_map_geometry_key(vertices, 5, sun_ray, 2048));
    assert(gfx_shadow_map_geometry_key(vertices, 0, sun_ray, 1024) == 0);
    assert(gfx_shadow_map_geometry_key((const float (*)[3])((float[][3]){{ NAN, 0, 0 }}), 1, sun_ray, 1024) == 0);
    puts("Directional shadow fit, projection, stabilization, and cache-key checks passed");
    return 0;
}
