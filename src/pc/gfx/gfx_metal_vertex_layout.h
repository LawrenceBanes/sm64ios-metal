#ifndef GFX_METAL_VERTEX_LAYOUT_H
#define GFX_METAL_VERTEX_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#include "gfx_cc.h"

enum GfxMetalVertexFormat {
    GFX_METAL_VERTEX_FLOAT2,
    GFX_METAL_VERTEX_FLOAT3,
    GFX_METAL_VERTEX_FLOAT4,
};

struct GfxMetalVertexLayout {
    uint8_t format[9];
    uint16_t offset[9];
    uint8_t attribute_count;
    uint16_t stride;
    uint8_t uv_attribute;
    uint8_t uv1_attribute;
    uint8_t fog_attribute;
    uint8_t normal_attribute;
    uint8_t input_attribute[4];
    bool has_uv;
    bool has_fog;
};

/* SM64's software vertex path multiplies row vectors by modelview matrices.
 * Transform a row-vector normal by inverse-transpose(A), where A is the
 * modelview linear part. Singular transforms and zero normals are marked
 * invalid so custom materials can retain the original combiner color. */
static inline bool gfx_metal_transform_view_normal(const float normal[3],
                                                    const float modelview[4][4],
                                                    float view_normal[3]) {
    if (!normal || !modelview || !view_normal) return false;
    float a = modelview[0][0], b = modelview[0][1], c = modelview[0][2];
    float d = modelview[1][0], e = modelview[1][1], f = modelview[1][2];
    float g = modelview[2][0], h = modelview[2][1], i = modelview[2][2];
    float cofactors[3][3] = {
        { e * i - f * h, f * g - d * i, d * h - e * g },
        { c * h - b * i, a * i - c * g, b * g - a * h },
        { b * f - c * e, c * d - a * f, a * e - b * d }
    };
    float determinant = a * cofactors[0][0] + b * cofactors[0][1] + c * cofactors[0][2];
    if (!isfinite(determinant) || fabsf(determinant) < 1.0e-8f) return false;
    float transformed[3];
    for (int column = 0; column < 3; ++column)
        transformed[column] = (normal[0] * cofactors[0][column] +
                               normal[1] * cofactors[1][column] +
                               normal[2] * cofactors[2][column]) / determinant;
    float length_squared = transformed[0] * transformed[0] +
                           transformed[1] * transformed[1] +
                           transformed[2] * transformed[2];
    if (!isfinite(length_squared) || length_squared < 1.0e-12f) return false;
    float inverse_length = 1.0f / sqrtf(length_squared);
    for (int axis = 0; axis < 3; ++axis) {
        view_normal[axis] = transformed[axis] * inverse_length;
        if (!isfinite(view_normal[axis])) return false;
    }
    return true;
}

/* Direction vectors use the view rotation only (translation is ignored). */
static inline bool gfx_metal_transform_view_direction(const float direction[3],
                                                      const float view[4][4],
                                                      float view_direction[3]) {
    if (!direction || !view || !view_direction) return false;
    float transformed[3];
    for (int column = 0; column < 3; ++column)
        transformed[column] = direction[0] * view[0][column] +
                              direction[1] * view[1][column] +
                              direction[2] * view[2][column];
    float length_squared = transformed[0] * transformed[0] +
                           transformed[1] * transformed[1] +
                           transformed[2] * transformed[2];
    if (!isfinite(length_squared) || length_squared < 1.0e-12f) return false;
    float inverse_length = 1.0f / sqrtf(length_squared);
    for (int axis = 0; axis < 3; ++axis) {
        view_direction[axis] = transformed[axis] * inverse_length;
        if (!isfinite(view_direction[axis])) return false;
    }
    return true;
}

/* Shared by MSL source generation and MTLVertexDescriptor construction so the
 * explicit shader slots and the packed CPU vertex stream cannot drift apart. */
static inline bool gfx_metal_get_vertex_layout(const struct CCFeatures *features,
                                                struct GfxMetalVertexLayout *layout) {
    uint16_t offset = 0;
    uint8_t count = 0;
    if (!features || !layout || features->num_inputs < 0 || features->num_inputs > 4)
        return false;
    layout->has_uv = features->used_textures[0] || features->used_textures[1];
    layout->has_fog = features->opt_fog;
    layout->uv_attribute = UINT8_MAX;
    layout->uv1_attribute = UINT8_MAX;
    layout->fog_attribute = UINT8_MAX;
    layout->normal_attribute = UINT8_MAX;
    layout->attribute_count = 0;
    layout->stride = 0;

    layout->format[count] = GFX_METAL_VERTEX_FLOAT4;
    layout->offset[count++] = offset;
    offset += 4 * sizeof(float);
    if (layout->has_uv) {
        layout->uv_attribute = count;
        layout->format[count] = GFX_METAL_VERTEX_FLOAT2;
        layout->offset[count++] = offset;
        offset += 2 * sizeof(float);
    }
    if (features->scene_material && features->used_textures[1]) {
        layout->uv1_attribute = count;
        layout->format[count] = GFX_METAL_VERTEX_FLOAT2;
        layout->offset[count++] = offset;
        offset += 2 * sizeof(float);
    }
    if (layout->has_fog) {
        layout->fog_attribute = count;
        layout->format[count] = GFX_METAL_VERTEX_FLOAT4;
        layout->offset[count++] = offset;
        offset += 4 * sizeof(float);
    }
    if (features->material_normals) {
        layout->normal_attribute = count;
        layout->format[count] = GFX_METAL_VERTEX_FLOAT4;
        layout->offset[count++] = offset;
        offset += 4 * sizeof(float);
    }
    for (int i = 0; i < features->num_inputs; ++i) {
        layout->input_attribute[i] = count;
        layout->format[count] = features->opt_alpha ? GFX_METAL_VERTEX_FLOAT4 : GFX_METAL_VERTEX_FLOAT3;
        layout->offset[count++] = offset;
        offset += (features->opt_alpha ? 4 : 3) * sizeof(float);
    }
    layout->attribute_count = count;
    layout->stride = offset;
    return true;
}

static inline bool gfx_metal_vertex_buffer_size_matches(const struct GfxMetalVertexLayout *layout,
                                                         size_t float_count,
                                                         size_t triangle_count) {
    if (!layout || layout->stride == 0 ||
        float_count > SIZE_MAX / sizeof(float) ||
        triangle_count > SIZE_MAX / (3u * layout->stride))
        return false;
    return float_count * sizeof(float) == triangle_count * 3u * layout->stride;
}

#endif
