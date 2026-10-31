#ifndef GFX_METAL_VERTEX_LAYOUT_H
#define GFX_METAL_VERTEX_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_cc.h"

enum GfxMetalVertexFormat {
    GFX_METAL_VERTEX_FLOAT2,
    GFX_METAL_VERTEX_FLOAT3,
    GFX_METAL_VERTEX_FLOAT4,
};

struct GfxMetalVertexLayout {
    uint8_t format[7];
    uint16_t offset[7];
    uint8_t attribute_count;
    uint16_t stride;
    uint8_t uv_attribute;
    uint8_t fog_attribute;
    uint8_t input_attribute[4];
    bool has_uv;
    bool has_fog;
};

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
    layout->fog_attribute = UINT8_MAX;
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
    if (layout->has_fog) {
        layout->fog_attribute = count;
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
