#include <metal_stdlib>
using namespace metal;

float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    if (params.x < 0.5) return baseColor;
    switch (materialClass) {
        case 1u: return float4(0.12, 0.72, 0.24, baseColor.a); /* world */
        case 2u: return float4(0.95, 0.68, 0.08, baseColor.a); /* object */
        case 3u: return float4(0.86, 0.16, 0.64, baseColor.a); /* character */
        default: return float4(0.36, 0.38, 0.42, baseColor.a);
    }
}
