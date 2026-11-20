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
                             uint2 tid [[thread_position_in_grid]]) {
    uint2 outputSize = uint2(output.get_width(), output.get_height());
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    uint2 inputSize = uint2(image.get_width(), image.get_height());
    uint2 sourcePoint = min(tid * inputSize / outputSize, inputSize - 1u);
    float2 uv = (float2(tid) + 0.5) / float2(outputSize);
    float depth = sceneDepth.sample(depthSampler, uv);
    float4 source = image.read(sourcePoint);
    if (depth >= 0.999999) {
        output.write(source, tid);
        return;
    }

    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0,
                         depth * 2.0 - 1.0, 1.0);
    float4 view = inverseProjection * clip;
    float distance = length(view.xyz / max(abs(view.w), 0.0001));
    float3 normal = normalize(sceneNormals.sample(normalSampler, uv).xyz * 2.0 - 1.0);
    float diffuse = 0.38 + 0.62 * max(normal.z, 0.0);
    float distanceBlend = clamp(params.x * smoothstep(12.0, 64.0, distance), 0.0, 0.55);
    float frameModulation = 0.995 + 0.005 * sin(frameInfo.x * 0.35);
    float3 shaded = mix(source.rgb * diffuse, float3(0.52, 0.67, 0.82), distanceBlend);
    output.write(float4(shaded * frameModulation, source.a), tid);
}
