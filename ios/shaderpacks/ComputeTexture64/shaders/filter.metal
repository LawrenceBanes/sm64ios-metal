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
                             uint2 tid [[thread_position_in_grid]]) {
    uint2 outputSize = uint2(output.get_width(), output.get_height());
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;
    uint2 inputSize = uint2(image.get_width(), image.get_height());
    uint2 sourcePoint = min(tid * inputSize / outputSize, inputSize - 1u);
    float4 base = image.read(sourcePoint);

    uint2 scroll = uint2(floor(frameInfo.x * 16.0));
    uint2 p0 = (tid + scroll) % uint2(packTexture0.get_width(), packTexture0.get_height());
    uint2 p1 = (tid + scroll) % uint2(packTexture1.get_width(), packTexture1.get_height());
    uint2 p2 = (tid + scroll) % uint2(packTexture2.get_width(), packTexture2.get_height());
    uint2 p3 = (tid + scroll) % uint2(packTexture3.get_width(), packTexture3.get_height());
    float3 pattern = packTexture0.read(p0).rgb;
    if (packTextureCount > 1u) pattern = packTexture1.read(p1).rgb;
    if (packTextureCount > 2u) pattern = packTexture2.read(p2).rgb;
    if (packTextureCount > 3u) pattern = packTexture3.read(p3).rgb;

    output.write(float4(mix(base.rgb, base.rgb * pattern * 1.5, clamp(params.x, 0.0, 1.0)), base.a), tid);
}
