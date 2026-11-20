#include <metal_stdlib>
using namespace metal;

static float3 shader64_reconstruct_view(float2 uv, float depth,
                                        constant float4x4 &inverseProjection) {
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0,
                         depth * 2.0 - 1.0, 1.0);
    float4 view = inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1.0e-6) * sign(view.w);
}

float4 shader64_pass_depth(float4 color, float2 uv, texture2d<float> image,
                           sampler imageSampler, depth2d<float> sceneDepth,
                           sampler depthSampler, constant float4x4 &inverseProjection,
                           constant float4 &params) {
    (void)color;
    (void)image;
    (void)imageSampler;
    (void)params;
    float centerDepth = sceneDepth.sample(depthSampler, uv);
    if (centerDepth >= 0.999999) return float4(0.0, 0.0, 0.0, 1.0);
    float2 texel = 1.0 / float2(sceneDepth.get_width(), sceneDepth.get_height());
    float2 uvL = clamp(uv - float2(texel.x, 0.0), 0.0, 1.0);
    float2 uvR = clamp(uv + float2(texel.x, 0.0), 0.0, 1.0);
    float2 uvU = clamp(uv - float2(0.0, texel.y), 0.0, 1.0);
    float2 uvD = clamp(uv + float2(0.0, texel.y), 0.0, 1.0);
    float dL = sceneDepth.sample(depthSampler, uvL);
    float dR = sceneDepth.sample(depthSampler, uvR);
    float dU = sceneDepth.sample(depthSampler, uvU);
    float dD = sceneDepth.sample(depthSampler, uvD);
    if (max(max(dL, dR), max(dU, dD)) >= 0.999999)
        return float4(0.5, 0.5, 1.0, 1.0);
    float3 pL = shader64_reconstruct_view(uvL, dL, inverseProjection);
    float3 pR = shader64_reconstruct_view(uvR, dR, inverseProjection);
    float3 pU = shader64_reconstruct_view(uvU, dU, inverseProjection);
    float3 pD = shader64_reconstruct_view(uvD, dD, inverseProjection);
    float3 normal = normalize(cross(pR - pL, pD - pU));
    float3 center = shader64_reconstruct_view(uv, centerDepth, inverseProjection);
    if (dot(normal, -center) < 0.0) normal = -normal;
    return float4(normal * 0.5 + 0.5, 1.0);
}
