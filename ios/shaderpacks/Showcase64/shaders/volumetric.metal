#include <metal_stdlib>
using namespace metal;

/* Half-resolution single-frame aerial perspective. This is a bounded
 * screen-space ray march against the existing directional shadow map; it is
 * not path tracing and has no temporal accumulation. */
kernel void shader64_compute(texture2d<float, access::read> image [[texture(0)]],
                             texture2d<float, access::write> output [[texture(1)]],
                             constant float4 &params [[buffer(0)]],
                             constant float4 &frameInfo [[buffer(2)]],
                             depth2d<float> sceneDepth [[texture(6)]],
                             sampler depthSampler [[sampler(6)]],
                             constant float4x4 &inverseProjection [[buffer(3)]],
                             texture2d<float> sceneNormals [[texture(7)]],
                             sampler normalSampler [[sampler(7)]],
                             constant float4x4 &inverseView [[buffer(4)]],
                             constant float4 &sunDirectionAndIntensity [[buffer(5)]],
                             depth2d<float> shadowMap [[texture(8)]],
                             sampler shadowSampler [[sampler(8)]],
                             constant float4 &shadowRightMin [[buffer(6)]],
                             constant float4 &shadowUpMin [[buffer(7)]],
                             constant float4 &shadowForwardMin [[buffer(8)]],
                             constant float4 &shadowRanges [[buffer(9)]],
                             uint2 tid [[thread_position_in_grid]]) {
    uint2 outputSize = uint2(output.get_width(), output.get_height());
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    uint2 inputSize = uint2(image.get_width(), image.get_height());
    if (inputSize.x == 0u || inputSize.y == 0u) return;
    float2 uv = (float2(tid) + 0.5) / float2(outputSize);
    /* Keep the required scene-color input observably part of the contract and
     * use its dimensions to reject invalid dispatch state. The full-resolution
     * color itself is sampled only by the final composition node. */
    if (uv.x < 0.0 || uv.y < 0.0 || image.get_width() == 0u) {
        output.write(float4(0.0), tid);
        return;
    }

    float depth = sceneDepth.sample(depthSampler, uv);
    float2 ndcXY = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 rayView = inverseProjection * float4(ndcXY, 1.0, 1.0);
    float rayW = abs(rayView.w) > 1.0e-5 ? rayView.w : 1.0e-5;
    float3 rayViewDirection = normalize(rayView.xyz / rayW);
    float3 rayWorldRaw = (inverseView * float4(rayViewDirection, 0.0)).xyz;
    float rayLength2 = dot(rayWorldRaw, rayWorldRaw);
    if (!isfinite(rayLength2) || rayLength2 < 1.0e-8) {
        output.write(float4(0.0, 0.0, 0.0, 1.0), tid);
        return;
    }
    float3 rayWorld = rayWorldRaw * rsqrt(rayLength2);
    float4 cameraH = inverseView * float4(0.0, 0.0, 0.0, 1.0);
    float cameraW = abs(cameraH.w) > 1.0e-5 ? cameraH.w : 1.0;
    float3 cameraWorld = cameraH.xyz / cameraW;

    float maxDistance = 150.0 * clamp(isfinite(params.z) ? params.z : 1.0, 0.25, 1.0);
    float endDistance = maxDistance;
    if (depth < 0.999999) {
        float4 clip = float4(ndcXY, depth * 2.0 - 1.0, 1.0);
        float4 viewH = inverseProjection * clip;
        float viewW = abs(viewH.w) > 1.0e-5 ? viewH.w : 1.0e-5;
        float3 viewPosition = viewH.xyz / viewW;
        float4 worldH = inverseView * float4(viewPosition, 1.0);
        float worldW = abs(worldH.w) > 1.0e-5 ? worldH.w : 1.0;
        float3 worldPosition = worldH.xyz / worldW;
        endDistance = min(length(worldPosition - cameraWorld), maxDistance);
    }

    float density = clamp(isfinite(params.x) ? params.x : 0.018, 0.004, 0.06);
    float3 encodedNormal = sceneNormals.sample(normalSampler, uv).xyz * 2.0 - 1.0;
    float normalConfidence = clamp(length(encodedNormal), 0.0, 1.0);
    if (depth < 0.999999) density *= mix(0.85, 1.0, normalConfidence);
    float strength = clamp(isfinite(params.y) ? params.y : 1.0, 0.0, 2.0);
    float sunLength = length(sunDirectionAndIntensity.xyz);
    /* Keep this authored showcase visibly lit with Classic settings, where
     * the optional global sun intensity normally starts at zero. */
    float sunIntensity = max(sunDirectionAndIntensity.w, 0.18);
    if (sunLength < 1.0e-5 || sunIntensity <= 0.0 || strength <= 0.0 || endDistance <= 0.0) {
        output.write(float4(0.0, 0.0, 0.0, 1.0), tid);
        return;
    }
    float3 toSun = sunDirectionAndIntensity.xyz / sunLength;
    float phaseCosine = clamp(dot(rayWorld, toSun), -0.98, 0.98);
    const float anisotropy = 0.72;
    float phaseDenom = max(1.0 + anisotropy * anisotropy - 2.0 * anisotropy * phaseCosine, 0.025);
    float phase = (1.0 - anisotropy * anisotropy) / (phaseDenom * sqrt(phaseDenom));
    phase = clamp(phase * 0.22, 0.08, 2.5);

    constexpr uint sampleCount = 8u;
    float stepLength = endDistance / float(sampleCount);
    float transmittance = 1.0;
    float3 radiance = float3(0.0);
    float3 sunTint = float3(1.00, 0.78, 0.54);
    float bias = max(shadowRanges.w, 0.0015);
    float2 texel = 1.0 / float2(shadowMap.get_width(), shadowMap.get_height());

    for (uint i = 0u; i < sampleCount; ++i) {
        float t = (float(i) + 0.5) * stepLength;
        float3 samplePosition = cameraWorld + rayWorld * t;
        float sx = (dot(shadowRightMin.xyz, samplePosition) - shadowRightMin.w) / shadowRanges.x;
        float sy = (dot(shadowUpMin.xyz, samplePosition) - shadowUpMin.w) / shadowRanges.y;
        float sz = (dot(shadowForwardMin.xyz, samplePosition) - shadowForwardMin.w) / shadowRanges.z;
        float2 shadowUV = float2(sx, 1.0 - sy);
        float visibility = 1.0;
        if (all(shadowUV >= 0.0) && all(shadowUV <= 1.0) && sz >= 0.0 && sz <= 1.0) {
            /* A compact 2x2 PCF tap helps suppress blocky shafts without a
             * costly large kernel on the mobile half-resolution pass. */
            visibility = 0.0;
            visibility += sz - bias <= shadowMap.sample(shadowSampler, shadowUV + float2(-0.5, -0.5) * texel) ? 0.25 : 0.0;
            visibility += sz - bias <= shadowMap.sample(shadowSampler, shadowUV + float2( 0.5, -0.5) * texel) ? 0.25 : 0.0;
            visibility += sz - bias <= shadowMap.sample(shadowSampler, shadowUV + float2(-0.5,  0.5) * texel) ? 0.25 : 0.0;
            visibility += sz - bias <= shadowMap.sample(shadowSampler, shadowUV + float2( 0.5,  0.5) * texel) ? 0.25 : 0.0;
        }
        float segment = transmittance * (1.0 - exp(-density * stepLength));
        float stability = 0.995 + 0.005 * sin(frameInfo.x * 0.23 + uv.x * 7.0 + uv.y * 5.0);
        radiance += sunTint * (segment * visibility * phase * sunIntensity * strength * 18.0 * stability);
        transmittance *= exp(-density * stepLength);
        if (transmittance < 0.015) break;
    }
    output.write(float4(min(radiance, float3(0.85)), clamp(transmittance, 0.0, 1.0)), tid);
}
