#include "gfx_shadow_map.h"

#include <math.h>
#include <string.h>

static void hash_bytes(uint64_t *hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= bytes[i];
        *hash *= UINT64_C(1099511628211);
    }
}

uint64_t gfx_shadow_map_geometry_key(const float (*positions)[3], size_t count,
                                     const float light_direction[3], uint32_t resolution) {
    if (!positions || count == 0 || !light_direction || resolution == 0) return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    hash_bytes(&hash, &count, sizeof(count));
    hash_bytes(&hash, &resolution, sizeof(resolution));
    hash_bytes(&hash, light_direction, sizeof(float) * 3u);
    for (size_t i = 0; i < count; ++i) {
        for (size_t axis = 0; axis < 3; ++axis) {
            if (!isfinite(positions[i][axis])) return 0;
        }
        hash_bytes(&hash, positions[i], sizeof(float) * 3u);
    }
    return hash ? hash : UINT64_C(1);
}

static float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool normalize3(float v[3]) {
    float length = sqrtf(dot3(v, v));
    if (!isfinite(length) || length < 1.0e-6f) return false;
    v[0] /= length; v[1] /= length; v[2] /= length;
    return true;
}

bool gfx_shadow_map_transform_point(const float object_transform[4][4],
                                    const float local_position[3], float world_position[3]) {
    if (!object_transform || !local_position || !world_position) return false;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!isfinite(object_transform[r][c])) return false;
    for (int c = 0; c < 3; ++c) {
        if (!isfinite(local_position[c])) return false;
        world_position[c] = local_position[0] * object_transform[0][c] +
                            local_position[1] * object_transform[1][c] +
                            local_position[2] * object_transform[2][c] +
                            object_transform[3][c];
        if (!isfinite(world_position[c])) return false;
    }
    return true;
}

bool gfx_shadow_map_fit(const float (*positions)[3], size_t count,
                        const float light_direction[3], uint32_t resolution,
                        struct GfxShadowMapTransform *transform) {
    if (!positions || count == 0 || !light_direction || !transform || resolution < 16u || resolution > 8192u)
        return false;
    float forward[3] = { light_direction[0], light_direction[1], light_direction[2] };
    if (!normalize3(forward)) return false;
    const float world_up[3] = { 0.0f, 1.0f, 0.0f };
    float right[3] = { world_up[1] * forward[2] - world_up[2] * forward[1],
                       world_up[2] * forward[0] - world_up[0] * forward[2],
                       world_up[0] * forward[1] - world_up[1] * forward[0] };
    if (!normalize3(right)) {
        const float alternate_up[3] = { 0.0f, 0.0f, 1.0f };
        right[0] = alternate_up[1] * forward[2] - alternate_up[2] * forward[1];
        right[1] = alternate_up[2] * forward[0] - alternate_up[0] * forward[2];
        right[2] = alternate_up[0] * forward[1] - alternate_up[1] * forward[0];
        if (!normalize3(right)) return false;
    }
    float up[3] = { forward[1] * right[2] - forward[2] * right[1],
                    forward[2] * right[0] - forward[0] * right[2],
                    forward[0] * right[1] - forward[1] * right[0] };
    if (!normalize3(up)) return false;

    float minv[3] = { INFINITY, INFINITY, INFINITY };
    float maxv[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < 3; ++c) if (!isfinite(positions[i][c])) return false;
        float x = dot3(right, positions[i]);
        float y = dot3(up, positions[i]);
        float z = dot3(forward, positions[i]);
        float value[3] = { x, y, z };
        for (int c = 0; c < 3; ++c) {
            if (value[c] < minv[c]) minv[c] = value[c];
            if (value[c] > maxv[c]) maxv[c] = value[c];
        }
    }
    float half_extent = fmaxf(maxv[0] - minv[0], maxv[1] - minv[1]) * 0.5f;
    if (!isfinite(half_extent)) return false;
    if (half_extent < 32.0f) half_extent = 32.0f;
    half_extent = ceilf(half_extent / 16.0f) * 16.0f;
    float texel = (2.0f * half_extent) / (float)resolution;
    float center_x = (minv[0] + maxv[0]) * 0.5f;
    float center_y = (minv[1] + maxv[1]) * 0.5f;
    center_x = roundf(center_x / texel) * texel;
    center_y = roundf(center_y / texel) * texel;
    minv[0] = center_x - half_extent;
    minv[1] = center_y - half_extent;
    minv[2] -= 64.0f;
    maxv[2] += 64.0f;

    memcpy(transform->right, right, sizeof(right));
    memcpy(transform->up, up, sizeof(up));
    memcpy(transform->forward, forward, sizeof(forward));
    memcpy(transform->minimum, minv, sizeof(minv));
    /* The XY projection is the stabilized square, not the original AABB
     * width. Keep the maximum on both axes aligned with the snapped center. */
    transform->range[0] = transform->range[1] = 2.0f * half_extent;
    transform->range[2] = maxv[2] - minv[2];
    return isfinite(transform->range[0]) && isfinite(transform->range[1]) &&
           isfinite(transform->range[2]) && transform->range[0] > 0.0f &&
           transform->range[1] > 0.0f && transform->range[2] > 0.0f;
}

bool gfx_shadow_map_project(const struct GfxShadowMapTransform *transform,
                            const float position[3], float clip[3], float uv_depth[3]) {
    if (!transform || !position || !clip || !uv_depth) return false;
    for (int c = 0; c < 3; ++c) if (!isfinite(position[c]) ||
        !isfinite(transform->range[c]) || transform->range[c] <= 0.0f) return false;
    float light[3] = { dot3(transform->right, position), dot3(transform->up, position),
                       dot3(transform->forward, position) };
    for (int c = 0; c < 3; ++c) {
        float n = (light[c] - transform->minimum[c]) / transform->range[c];
        clip[c] = c < 2 ? 2.0f * n - 1.0f : n;
        uv_depth[c] = c == 1 ? 1.0f - n : n;
        if (!isfinite(clip[c]) || !isfinite(uv_depth[c])) return false;
    }
    return true;
}
