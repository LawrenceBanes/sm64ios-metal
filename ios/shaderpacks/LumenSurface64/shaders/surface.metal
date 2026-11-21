#include <metal_stdlib>
using namespace metal;

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
    uint2 sourcePoint = min(tid * inputSize / outputSize, inputSize - 1u);
    float2 uv = (float2(tid) + 0.5) / float2(outputSize);
    float4 source = image.read(sourcePoint);
    float depth = sceneDepth.sample(depthSampler, uv);
    if (depth >= 0.999999) {
        output.write(source, tid);
        return;
    }

    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0,
                         depth * 2.0 - 1.0, 1.0);
    float4 view = inverseProjection * clip;
    float distance = length(view.xyz / max(abs(view.w), 0.0001));
    float3 normal = normalize(sceneNormals.sample(normalSampler, uv).xyz * 2.0 - 1.0);
    float3 worldNormal = normalize((inverseView * float4(normal, 0.0)).xyz);
    float4 viewPosition = view / max(abs(view.w), 0.0001);
    float4 worldHomogeneous = inverseView * viewPosition;
    float3 worldPosition = worldHomogeneous.xyz / max(abs(worldHomogeneous.w), 0.0001);
    float sunLength = length(sunDirectionAndIntensity.xyz);
    float noL = sunLength > 1.0e-6 ? abs(dot(worldNormal, sunDirectionAndIntensity.xyz / sunLength)) : 1.0;
    float slopeBias = shadowRanges.w * (1.0 + 2.0 * (1.0 - noL));

    float shadowVisibility = 1.0;
    float sx = (dot(shadowRightMin.xyz, worldPosition) - shadowRightMin.w) / shadowRanges.x;
    float sy = (dot(shadowUpMin.xyz, worldPosition) - shadowUpMin.w) / shadowRanges.y;
    float sz = (dot(shadowForwardMin.xyz, worldPosition) - shadowForwardMin.w) / shadowRanges.z;
    float2 shadowUV = float2(sx, 1.0 - sy);
    if (all(shadowUV >= 0.0) && all(shadowUV <= 1.0) && sz >= 0.0 && sz <= 1.0) {
        shadowVisibility = 0.0;
        float2 texel = 1.0 / float2(shadowMap.get_width(), shadowMap.get_height());
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                float storedDepth = shadowMap.sample(shadowSampler, shadowUV + float2(x, y) * texel);
                shadowVisibility += sz - slopeBias <= storedDepth ? (1.0 / 9.0) : 0.0;
            }
        }
    }
    float shadowFactor = mix(1.0, shadowVisibility, clamp(params.w, 0.0, 1.0));

    float fog = clamp(smoothstep(96.0, 240.0, distance) * 0.18, 0.0, 0.18);
    float3 composite = source.rgb * shadowFactor;
    output.write(float4(mix(composite, float3(0.52, 0.67, 0.82), fog), source.a), tid);
}
