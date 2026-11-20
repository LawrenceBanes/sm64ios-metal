#include <metal_stdlib>
using namespace metal;

kernel void shader64_compute(texture2d<float, access::read> image [[texture(0)]],
                             texture2d<float, access::write> output [[texture(1)]],
                             constant float4 &params [[buffer(0)]],
                             uint2 tid [[thread_position_in_grid]]) {
    if (tid.x >= output.get_width() || tid.y >= output.get_height()) return;
    uint2 inputSize = uint2(image.get_width(), image.get_height());
    float2 uv = (float2(tid) + 0.5) / float2(output.get_width(), output.get_height());
    uint2 center = min(uint2(uv * float2(inputSize)), inputSize - 1u);
    float4 blurred = float4(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            uint2 point = uint2(clamp(int2(center) + int2(x, y), int2(0), int2(inputSize) - 1));
            blurred += image.read(point) * (1.0 / 9.0);
        }
    }
    output.write(mix(image.read(center), blurred, clamp(params.x, 0.0, 1.0)), tid);
}
