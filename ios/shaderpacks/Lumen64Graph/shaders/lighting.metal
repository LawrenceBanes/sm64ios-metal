#include <metal_stdlib>
using namespace metal;

static float3 lumen64_view_position(float2 uv, float depth,
                                    constant float4x4 &inverseProjection) {
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0,
                         depth * 2.0 - 1.0, 1.0);
    float4 view = inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1.0e-6) * sign(view.w);
}

float4 shader64_pass_lit(float4 color, float2 uv, texture2d<float> image,
                         sampler imageSampler, depth2d<float> sceneDepth,
                         sampler depthSampler, constant float4x4 &inverseProjection,
                         constant float4x4 &inverseView,
                         float4 sunDirectionAndIntensity,
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
    if (max(max(leftDepth, rightDepth), max(upDepth, downDepth)) >= 0.999999)
        return color;
    float3 leftPos = lumen64_view_position(leftUV, leftDepth, inverseProjection);
    float3 rightPos = lumen64_view_position(rightUV, rightDepth, inverseProjection);
    float3 upPos = lumen64_view_position(upUV, upDepth, inverseProjection);
    float3 downPos = lumen64_view_position(downUV, downDepth, inverseProjection);
    float3 normalView = normalize(cross(rightPos - leftPos, downPos - upPos));
    float3 center = lumen64_view_position(uv, depth, inverseProjection);
    if (dot(normalView, -center) < 0.0) normalView = -normalView;
    float3 normalWorld = normalize((inverseView * float4(normalView, 0.0)).xyz);
    float3 sun = normalize(sunDirectionAndIntensity.xyz);
    float lambert = max(dot(normalWorld, sun), 0.0);
    float ambient = clamp(params.x, 0.0, 1.0);
    float direct = lambert * clamp(sunDirectionAndIntensity.w, 0.0, 1.0) * clamp(params.y, 0.0, 1.0);
    return float4(color.rgb * (ambient + direct), color.a);
}
