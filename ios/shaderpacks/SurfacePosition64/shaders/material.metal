#include <metal_stdlib>
using namespace metal;

/* Diagnostic that uses the actual submitted vertex position transformed by
 * the active modelview matrix. Invalid positions preserve the combiner color. */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, float3 viewPosition, bool positionValid,
                         constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    (void)materialClass;
    if (!positionValid) return baseColor;
    float3 positionColor = 0.5 + 0.5 * sin(viewPosition * max(params.x, 0.001));
    return float4(positionColor, baseColor.a);
}
