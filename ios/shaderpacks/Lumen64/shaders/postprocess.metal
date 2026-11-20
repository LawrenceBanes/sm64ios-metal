#include <metal_stdlib>
using namespace metal;

/* This reference is a screen-space tone curve only; it does not implement
 * global illumination, ray tracing, or add a new shadowing technique. */
float4 shader64_post(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    float exposure = exp2(clamp(params.x, -4.0, 4.0));
    float3 x = max(color.rgb * exposure, 0.0);
    float3 mapped = clamp((x * (2.51 * x + 0.03)) /
                          (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    float gray = dot(mapped, float3(0.2126, 0.7152, 0.0722));
    mapped = mix(float3(gray), mapped, clamp(params.y, 0.0, 2.0));
    return float4(mapped, color.a);
}
