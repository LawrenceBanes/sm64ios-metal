#include <metal_stdlib>
using namespace metal;

float4 shader64_pass_textured(float4 color, float2 uv, texture2d<float> image,
                              sampler imageSampler,
                              texture2d<float> packTexture0, sampler packSampler0,
                              texture2d<float> packTexture1, sampler packSampler1,
                              texture2d<float> packTexture2, sampler packSampler2,
                              texture2d<float> packTexture3, sampler packSampler3,
                              uint packTextureCount, constant float4 &params) {
    (void)image;
    (void)imageSampler;
    (void)packTexture1;
    (void)packSampler1;
    (void)packTexture2;
    (void)packSampler2;
    (void)packTexture3;
    (void)packSampler3;
    if (packTextureCount == 0u) return color;
    float2 checkerUV = fract(uv * 24.0);
    float3 pattern = packTexture0.sample(packSampler0, checkerUV).rgb;
    return float4(mix(color.rgb, color.rgb * pattern * 1.7, clamp(params.x, 0.0, 1.0)), color.a);
}
