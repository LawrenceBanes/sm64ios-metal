#include <metal_stdlib>
using namespace metal;

float4 shader64_pass_frame(float4 color, float2 uv, texture2d<float> image,
                           sampler imageSampler, constant float4 &params,
                           constant float4 &frameInfo) {
    (void)uv;
    (void)image;
    (void)imageSampler;
    float pulse = 1.0 + clamp(params.x, 0.0, 0.35) * sin(frameInfo.x * 2.0);
    return float4(color.rgb * pulse, color.a);
}
