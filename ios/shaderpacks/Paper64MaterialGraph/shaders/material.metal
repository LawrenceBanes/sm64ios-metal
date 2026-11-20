#include <metal_stdlib>
using namespace metal;

float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    float levels = clamp(floor(params.x + 0.5), 2.0, 8.0);
    float luminance = dot(baseColor.rgb, float3(0.299, 0.587, 0.114));
    float quantized = floor(clamp(luminance, 0.0, 1.0) * levels + 0.5) / levels;
    float shade = quantized / max(luminance, 0.06);
    float toonStrength = clamp(params.y, 0.0, 1.0);
    float3 toonColor = mix(baseColor.rgb, baseColor.rgb * shade, toonStrength);
    if (materialClass == 3u) {
        toonColor.r += params.z;
        toonColor.b += max(params.z, 0.0) * 0.35;
    }
    return float4(clamp(toonColor, 0.0, 1.0), baseColor.a);
}
