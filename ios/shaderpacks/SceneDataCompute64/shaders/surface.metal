#include <metal_stdlib>
using namespace metal;

kernel void shader64_compute(texture2d<float, access::read> image [[texture(0)]],
                             texture2d<float, access::write> output [[texture(1)]],
                             texture2d<float> packTexture0 [[texture(2)]],
                             texture2d<float> packTexture1 [[texture(3)]],
                             texture2d<float> packTexture2 [[texture(4)]],
                             texture2d<float> packTexture3 [[texture(5)]],
                             constant float4 &params [[buffer(0)]],
                             constant uint &packTextureCount [[buffer(1)]],
                             constant float4 &frameInfo [[buffer(2)]],
                             depth2d<float> sceneDepth [[texture(6)]],
                             sampler depthSampler [[sampler(6)]],
                             constant float4x4 &inverseProjection [[buffer(3)]],
                             texture2d<float> sceneNormals [[texture(7)]],
                             sampler normalSampler [[sampler(7)]],
                             constant float4x4 &inverseView [[buffer(4)]],
                             constant float4 &sunDirectionAndIntensity [[buffer(5)]],
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
    float diffuse = 0.38 + 0.62 * max(dot(worldNormal, normalize(sunDirectionAndIntensity.xyz)), 0.0) *
                    clamp(sunDirectionAndIntensity.w, 0.0, 1.0);

    float4 pattern = float4(1.0);
    uint2 textureSize = uint2(packTexture0.get_width(), packTexture0.get_height());
    uint2 texturePoint = min(uint2(uv * float2(textureSize)), textureSize - 1u);
    pattern = packTexture0.read(texturePoint);
    if (packTextureCount > 1u) pattern = packTexture1.read(min(uint2(uv * float2(packTexture1.get_width(), packTexture1.get_height())), uint2(packTexture1.get_width() - 1u, packTexture1.get_height() - 1u)));
    if (packTextureCount > 2u) pattern = packTexture2.read(min(uint2(uv * float2(packTexture2.get_width(), packTexture2.get_height())), uint2(packTexture2.get_width() - 1u, packTexture2.get_height() - 1u)));
    if (packTextureCount > 3u) pattern = packTexture3.read(min(uint2(uv * float2(packTexture3.get_width(), packTexture3.get_height())), uint2(packTexture3.get_width() - 1u, packTexture3.get_height() - 1u)));

    float textureMix = clamp(params.x, 0.0, 1.0);
    float fog = clamp(params.y * smoothstep(12.0, 64.0, distance), 0.0, 0.55);
    float frameModulation = 0.995 + 0.005 * sin(frameInfo.x * 0.35);
    float3 lit = mix(source.rgb * diffuse, source.rgb * pattern.rgb, textureMix);
    output.write(float4(mix(lit, float3(0.52, 0.67, 0.82), fog) * frameModulation, source.a), tid);
}
