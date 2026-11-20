#ifndef GFX_SHADOW_MAP_H
#define GFX_SHADOW_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct GfxShadowMapTransform {
    float right[3];
    float up[3];
    float forward[3];
    float minimum[3];
    float range[3];
};

/* Applies the engine's row-vector object transform to a captured local point. */
bool gfx_shadow_map_transform_point(const float object_transform[4][4],
                                    const float local_position[3], float world_position[3]);
/* Fits a stabilized directional-light box around a bounded world-space vertex set. */
bool gfx_shadow_map_fit(const float (*positions)[3], size_t count,
                        const float light_direction[3], uint32_t resolution,
                        struct GfxShadowMapTransform *transform);
bool gfx_shadow_map_project(const struct GfxShadowMapTransform *transform,
                            const float position[3], float clip[3], float uv_depth[3]);

#endif
