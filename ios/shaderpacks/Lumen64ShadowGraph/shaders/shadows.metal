#include <metal_stdlib>
using namespace metal;

static float3 lumen64_shadow_view_position(float2 uv, float depth,
                                           constant float4x4 &inverseProjection) {
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0,
                         depth * 2.0 - 1.0, 1.0);
    float4 view = inverseProjection * clip;
    float viewW = abs(view.w) < 1.0e-6 ? 1.0e-6 : view.w;
    return view.xyz / viewW;
}

static float lumen64_visibility(depth2d<float> shadowMap, sampler shadowSampler,
                                float3 worldPosition, float4 shadowRightMin,
                                float4 shadowUpMin, float4 shadowForwardMin,
                                float4 shadowRanges) {
    float sx = (dot(shadowRightMin.xyz, worldPosition) - shadowRightMin.w) / shadowRanges.x;
    float sy = (dot(shadowUpMin.xyz, worldPosition) - shadowUpMin.w) / shadowRanges.y;
    float sz = (dot(shadowForwardMin.xyz, worldPosition) - shadowForwardMin.w) / shadowRanges.z;
    float2 shadowUV = float2(sx, 1.0 - sy);
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0) || sz < 0.0 || sz > 1.0) return 1.0;
    float2 texel = 1.0 / float2(shadowMap.get_width(), shadowMap.get_height());
    float visible = 0.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        float stored = shadowMap.sample(shadowSampler, shadowUV + float2(x, y) * texel);
        visible += sz - shadowRanges.w <= stored ? 1.0 : 0.0;
    }
    return visible / 9.0;
}

float4 shader64_pass_shadowed(float4 color, float2 uv, texture2d<float> image,
                              sampler imageSampler, depth2d<float> sceneDepth,
                              sampler depthSampler, depth2d<float> shadowMap,
                              sampler shadowSampler,
                              constant float4x4 &inverseProjection,
                              constant float4x4 &inverseView,
                              float4 sunDirectionAndIntensity,
                              float4 shadowRightMin, float4 shadowUpMin,
                              float4 shadowForwardMin, float4 shadowRanges,
                              constant float4 &params) {
    (void)image;
    (void)imageSampler;
    float depth = sceneDepth.sample(depthSampler, uv);
    if (depth >= 0.999999) return color;
    float2 texel = 1.0 / float2(sceneDepth.get_width(), sceneDepth.get_height());
    float2 leftUV = clamp(uv - float2(texel.x, 0.0), 0.0, 1.0);
    float2 rightUV = clamp(uv + float2(texel.x, 0.0), 0.0, 1.0);
    float2 upUV = clamp(uv - float2(0.0, texel.y), 0.0, 1.0);
    float2 downUV = clamp(uv + float2(0.0, texel.y), 0.0, 1.0);
    float leftDepth = sceneDepth.sample(depthSampler, leftUV);
    float rightDepth = sceneDepth.sample(depthSampler, rightUV);
    float upDepth = sceneDepth.sample(depthSampler, upUV);
    float downDepth = sceneDepth.sample(depthSampler, downUV);
    if (max(max(leftDepth, rightDepth), max(upDepth, downDepth)) >= 0.999999) return color;
    float3 leftPos = lumen64_shadow_view_position(leftUV, leftDepth, inverseProjection);
    float3 rightPos = lumen64_shadow_view_position(rightUV, rightDepth, inverseProjection);
    float3 upPos = lumen64_shadow_view_position(upUV, upDepth, inverseProjection);
    float3 downPos = lumen64_shadow_view_position(downUV, downDepth, inverseProjection);
    float3 normalView = normalize(cross(rightPos - leftPos, downPos - upPos));
    float3 centerView = lumen64_shadow_view_position(uv, depth, inverseProjection);
    if (dot(normalView, -centerView) < 0.0) normalView = -normalView;
    float4 world = inverseView * float4(centerView, 1.0);
    float worldW = abs(world.w) < 1.0e-6 ? 1.0e-6 : world.w;
    world.xyz /= worldW;
    float3 normalWorld = normalize((inverseView * float4(normalView, 0.0)).xyz);
    float visibility = lumen64_visibility(shadowMap, shadowSampler, world.xyz,
        shadowRightMin, shadowUpMin, shadowForwardMin, shadowRanges);
    float diffuse = max(dot(normalWorld, normalize(sunDirectionAndIntensity.xyz)), 0.0);
    float light = clamp(params.x, 0.0, 1.0) +
        diffuse * clamp(sunDirectionAndIntensity.w, 0.0, 1.0) *
        clamp(params.y, 0.0, 1.0) * mix(0.62, 1.0, visibility);
    return float4(color.rgb * light, color.a);
}
