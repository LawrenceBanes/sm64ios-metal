#include "../src/pc/gfx/gfx_metal_shader.h"
#include "../src/pc/gfx/gfx_metal_vertex_layout.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t shader(uint8_t rgb[4], uint8_t alpha[4], uint32_t options) {
    uint32_t id = options;
    for (int i = 0; i < 4; ++i) {
        id |= (uint32_t)(rgb[i] & 7) << (i * 3);
        id |= (uint32_t)(alpha[i] & 7) << (12 + i * 3);
    }
    return id;
}

static void assert_dense_vertex_attributes(const char *source, const struct CCFeatures *features) {
    const char *p = source;
    int expected = 0;
    struct GfxMetalVertexLayout layout;
    assert(gfx_metal_get_vertex_layout(features, &layout));
    assert(layout.attribute_count == 1 + layout.has_uv + layout.has_fog + features->num_inputs);
    assert(layout.stride == 16 + (layout.has_uv ? 8 : 0) + (layout.has_fog ? 16 : 0) +
           features->num_inputs * (features->opt_alpha ? 16 : 12));
    uint16_t expected_offset = 0;
    for (int i = 0; i < layout.attribute_count; ++i) {
        assert(layout.offset[i] == expected_offset);
        switch (layout.format[i]) {
            case GFX_METAL_VERTEX_FLOAT2: expected_offset += 8; break;
            case GFX_METAL_VERTEX_FLOAT3: expected_offset += 12; break;
            case GFX_METAL_VERTEX_FLOAT4: expected_offset += 16; break;
            default: assert(0 && "invalid vertex format");
        }
    }
    assert(expected_offset == layout.stride);
    assert(layout.stride % sizeof(float) == 0);
    assert(gfx_metal_vertex_buffer_size_matches(&layout,
        (size_t)2 * 3 * layout.stride / sizeof(float), 2));
    assert(!gfx_metal_vertex_buffer_size_matches(&layout,
        (size_t)2 * 3 * layout.stride / sizeof(float) + 1, 2));
    assert(!gfx_metal_vertex_buffer_size_matches(&layout, 0, 1));
    assert(!gfx_metal_vertex_buffer_size_matches(&layout, 0, SIZE_MAX));
    while ((p = strstr(p, "[[attribute(")) != NULL) {
        int actual = -1;
        assert(sscanf(p, "[[attribute(%d)", &actual) == 1);
        assert(expected < layout.attribute_count);
        assert(actual == expected);
        ++expected;
        ++p;
    }
    assert(expected == layout.attribute_count);
}

int main(void) {
    char source[8192];
    struct CCFeatures features;
    const char *present_source = gfx_metal_present_shader_source();
    assert(strstr(present_source, "sm64_present_vertex") != NULL);
    assert(strstr(present_source, "sm64_present_fragment") != NULL);
    assert(strstr(present_source, "texture2d<float> image") != NULL);
    uint8_t flat_rgb[4] = { 0, 0, 0, SHADER_INPUT_1 };
    uint8_t flat_alpha[4] = { 0, 0, 0, SHADER_INPUT_1 };
    uint32_t id = shader(flat_rgb, flat_alpha, 0);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(features.num_inputs == 1);
    assert(strstr(source, "vertex VOut sm64_vertex") != NULL);
    assert(strstr(source, "float3 input1 [[attribute(1)]]") != NULL);
    assert(strstr(source, "float3(in.input1.rgb)") == NULL);
    assert(strstr(source, "float4 color=float4(in.input1.rgb,1.0)") != NULL);
    assert_dense_vertex_attributes(source, &features);

    /* The MSL input slots must be a dense match for pipeline_for()'s packed
     * descriptor: position, optional UV, optional fog, and then shade inputs. */
    uint8_t input2_rgb[4] = { 0, 0, 0, SHADER_INPUT_2 };
    id = shader(input2_rgb, flat_alpha, 0);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(features.num_inputs == 2);
    assert(strstr(source, "float3 input1 [[attribute(1)]]") != NULL);
    assert(strstr(source, "float3 input2 [[attribute(2)]]") != NULL);
    assert_dense_vertex_attributes(source, &features);

    id = shader(input2_rgb, flat_alpha, SHADER_OPT_ALPHA);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(strstr(source, "float4 input1 [[attribute(1)]]") != NULL);
    assert(strstr(source, "float4 input2 [[attribute(2)]]") != NULL);
    assert_dense_vertex_attributes(source, &features);

    id = shader(input2_rgb, flat_alpha, SHADER_OPT_FOG);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(strstr(source, "float4 fog [[attribute(1)]]") != NULL);
    assert(strstr(source, "float3 input1 [[attribute(2)]]") != NULL);
    assert(strstr(source, "float3 input2 [[attribute(3)]]") != NULL);
    assert_dense_vertex_attributes(source, &features);

    id = shader(input2_rgb, flat_alpha, SHADER_OPT_ALPHA | SHADER_OPT_FOG);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(strstr(source, "float4 fog [[attribute(1)]]") != NULL);
    assert(strstr(source, "float4 input1 [[attribute(2)]]") != NULL);
    assert(strstr(source, "float4 input2 [[attribute(3)]]") != NULL);
    assert_dense_vertex_attributes(source, &features);

    uint8_t texture_rgb[4] = { SHADER_TEXEL0, 0, 0, 0 };
    id = shader(texture_rgb, flat_alpha, SHADER_OPT_ALPHA | SHADER_OPT_FOG | SHADER_OPT_TEXTURE_EDGE);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(features.used_textures[0]);
    assert(features.opt_alpha && features.opt_fog && features.opt_texture_edge);
    assert(strstr(source, "texture2d<float> tex0 [[texture(0)]]") != NULL);
    assert(strstr(source, "float2 uv [[attribute(1)]]") != NULL);
    assert(strstr(source, "float4 fog [[attribute(2)]]") != NULL);
    assert(strstr(source, "float4 input1 [[attribute(3)]]") != NULL);
    assert_dense_vertex_attributes(source, &features);
    assert(strstr(source, "sampler samp0 [[sampler(0)]]") != NULL);
    assert(strstr(source, "FragmentParams &params [[buffer(1)]]") != NULL);
    assert(strstr(source, "sampleTex(tex0,samp0,in.uv,params.filter0!=0)") != NULL);
    assert(strstr(source, "offset-=step(1.0,offset.x+offset.y)") != NULL);
    assert(strstr(source, "discard_fragment()") != NULL);
    assert(strstr(source, "mix(color.rgb,in.fog.rgb,in.fog.a)") != NULL);
    assert(strstr(source, "diagnosticMode==1u?float4(1.0):sampleTex") != NULL);
    assert(strstr(source, "diagnosticMode==2u") != NULL);
    assert(strstr(source, "diagnosticTextureColor(params.textureId0)") != NULL);
    assert(strstr(source, "diagnosticMode==4u") != NULL);
    assert(strstr(source, "vertex_id [[vertex_id]]") != NULL);
    assert(strstr(source, "o.barycentric=") != NULL);

    /* No-texture variants still accept per-draw diagnostic parameters and
     * provide explicit outputs for UV and texture-ID inspection. */
    id = shader(flat_rgb, flat_alpha, 0);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(!features.used_textures[0] && !features.used_textures[1]);
    assert(strstr(source, "constant FragmentParams &params [[buffer(1)]]") != NULL);
    assert(strstr(source, "return float4(1.0,0.0,1.0,1.0)") != NULL);
    assert(strstr(source, "diagnosticMode==3u") != NULL);
    assert(strstr(source, "diagnosticMode==4u") != NULL);
    assert_dense_vertex_attributes(source, &features);

    /* A dual-texture variant substitutes both combiner inputs without
     * bypassing the existing fog/alpha path in normal mode. */
    id = 0x01a00a6fu;
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(features.used_textures[0] && features.used_textures[1]);
    assert(strstr(source, "diagnosticMode==1u?float4(1.0):sampleTex(tex0") != NULL);
    assert(strstr(source, "diagnosticMode==1u?float4(1.0):sampleTex(tex1") != NULL);
    assert(strstr(source, "diagnosticMode==2u") != NULL);
    assert(strstr(source, "diagnosticTextureColor(params.textureId1)") != NULL);
    assert_dense_vertex_attributes(source, &features);

    /* Reproduce the four shader IDs that failed on the device and check their
     * complete MSL attribute lists against the packed descriptor cardinality. */
    {
        const uint32_t device_failures[] = { 0x01200200u, 0x00000045u,
                                             0x00000200u, 0x01200a00u };
        for (size_t i = 0; i < sizeof(device_failures) / sizeof(device_failures[0]); ++i) {
            assert(gfx_metal_build_shader(device_failures[i], source, sizeof(source), &features));
            assert_dense_vertex_attributes(source, &features);
        }
    }

    /* Cover zero through four shade inputs across optional texture and fog
     * fields, with both RGB-only and alpha-carrying vertex formats. */
    for (int input_count = 0; input_count <= 4; ++input_count) {
        uint8_t rgb[4] = { 0, 0, 0, input_count ? (uint8_t)(SHADER_INPUT_1 + input_count - 1) : 0 };
        uint8_t alpha[4] = { 0, 0, 0, 0 };
        for (int texture = 0; texture < 2; ++texture) {
            for (int fog = 0; fog < 2; ++fog) {
                for (int alpha_format = 0; alpha_format < 2; ++alpha_format) {
                    uint8_t variant_rgb[4];
                    memcpy(variant_rgb, rgb, sizeof(rgb));
                    if (texture) variant_rgb[0] = SHADER_TEXEL0;
                    uint32_t options = (fog ? SHADER_OPT_FOG : 0) |
                                       (alpha_format ? SHADER_OPT_ALPHA : 0);
                    id = shader(variant_rgb, alpha, options);
                    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
                    assert_dense_vertex_attributes(source, &features);
                }
            }
        }
    }

    uint8_t noise_rgb[4] = { 0, 0, 0, SHADER_INPUT_1 };
    uint8_t noise_alpha[4] = { 0, 0, 0, SHADER_INPUT_1 };
    id = shader(noise_rgb, noise_alpha, SHADER_OPT_ALPHA | SHADER_OPT_NOISE);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(features.opt_noise && features.opt_alpha);
    assert(strstr(source, "randomValue(float3 value)") != NULL);
    assert(strstr(source, "constant FragmentParams &params [[buffer(1)]]") != NULL);
    assert(strstr(source, "in.pos.xy,params.frameCount") != NULL);
    assert(strstr(source, "color.a*=floor(randomValue(") != NULL);

    assert(!gfx_metal_build_shader(id, source, 8, &features));
    assert(!gfx_metal_build_shader(id, NULL, sizeof(source), &features));
    puts("Metal shader source generation checks passed");
    return 0;
}
