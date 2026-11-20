#include <metal_stdlib>
using namespace metal;

float4 shader64_pass(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    float levels = max(2.0, params.x);
    float3 stepped = floor(color.rgb * levels + 0.5) / levels;
    return float4(mix(color.rgb, stepped, clamp(params.y, 0.0, 1.0)), color.a);
}
