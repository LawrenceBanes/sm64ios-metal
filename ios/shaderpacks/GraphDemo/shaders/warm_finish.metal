#include <metal_stdlib>
using namespace metal;

float4 shader64_pass(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    float3 tint = float3(1.0, 0.94, 0.82);
    return float4(mix(color.rgb, color.rgb * tint, clamp(params.z, 0.0, 1.0)), color.a);
}
