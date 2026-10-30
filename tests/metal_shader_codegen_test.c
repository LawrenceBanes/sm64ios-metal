#include "../src/pc/gfx/gfx_metal_shader.h"

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

int main(void) {
    char source[8192];
    struct CCFeatures features;
    uint8_t flat_rgb[4] = { 0, 0, 0, SHADER_INPUT_1 };
    uint8_t flat_alpha[4] = { 0, 0, 0, SHADER_INPUT_1 };
    uint32_t id = shader(flat_rgb, flat_alpha, 0);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(features.num_inputs == 1);
    assert(strstr(source, "vertex VOut sm64_vertex") != NULL);
    assert(strstr(source, "float3 input1 [[attribute(2)]]") != NULL);
    assert(strstr(source, "float3(in.input1.rgb)") == NULL);
    assert(strstr(source, "float4 color=float4(in.input1.rgb,1.0)") != NULL);

    uint8_t texture_rgb[4] = { SHADER_TEXEL0, 0, 0, 0 };
    id = shader(texture_rgb, flat_alpha, SHADER_OPT_ALPHA | SHADER_OPT_FOG | SHADER_OPT_TEXTURE_EDGE);
    assert(gfx_metal_build_shader(id, source, sizeof(source), &features));
    assert(features.used_textures[0]);
    assert(features.opt_alpha && features.opt_fog && features.opt_texture_edge);
    assert(strstr(source, "texture2d<float> tex0 [[texture(0)]]") != NULL);
    assert(strstr(source, "sampler samp0 [[sampler(0)]]") != NULL);
    assert(strstr(source, "FragmentParams &params [[buffer(1)]]") != NULL);
    assert(strstr(source, "sampleTex(tex0,samp0,in.uv,params.filter0!=0)") != NULL);
    assert(strstr(source, "offset-=step(1.0,offset.x+offset.y)") != NULL);
    assert(strstr(source, "discard_fragment()") != NULL);
    assert(strstr(source, "mix(color.rgb,in.fog.rgb,in.fog.a)") != NULL);

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
