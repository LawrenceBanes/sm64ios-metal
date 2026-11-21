#include <metal_stdlib>
using namespace metal;

/* Diagnostic only: compare untouched scene color (left) with a hypothetical
 * sRGB-to-linear power decode (center) and linear-to-display power encode
 * (right). This intentionally does not assert which transform is correct. */
float4 shader64_pass(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    float3 raw = image.sample(imageSampler, uv).rgb;
    float3 compared = raw;
    if (uv.x >= (1.0 / 3.0) && uv.x < (2.0 / 3.0))
        compared = pow(max(raw, float3(0.0)), float3(2.2));
    else if (uv.x >= (2.0 / 3.0))
        compared = pow(max(raw, float3(0.0)), float3(1.0 / 2.2));
    float edge = min(abs(uv.x - (1.0 / 3.0)), abs(uv.x - (2.0 / 3.0)));
    if (edge < 0.0015) compared = float3(1.0, 0.0, 1.0);
    return float4(compared, 1.0);
}
