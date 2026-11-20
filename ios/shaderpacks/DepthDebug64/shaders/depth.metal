#include <metal_stdlib>
using namespace metal;

/* Raw Metal depth is nonlinear in [0, 1]. Use inverseProjection to
 * reconstruct view-space position for custom linear-depth displays. */
float4 shader64_pass_depth(float4 color, float2 uv, texture2d<float> image,
                           sampler imageSampler, depth2d<float> sceneDepth,
                           sampler depthSampler, constant float4x4 &inverseProjection,
                           constant float4 &params) {
    (void)color;
    (void)image;
    (void)imageSampler;
    (void)inverseProjection;
    float depth = sceneDepth.sample(depthSampler, uv);
    float nearMask = 1.0 - step(0.999999, depth);
    return float4(float3(depth * nearMask), 1.0);
}
