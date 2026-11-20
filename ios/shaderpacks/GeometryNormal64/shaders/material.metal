#include <metal_stdlib>
using namespace metal;

/* Visualize a geometric face normal derived from true view-space positions.
 * This is a flat, screen-space derivative normal, not a recovered smooth normal. */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, float3 viewPosition, bool positionValid,
                         constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    (void)materialClass;
    if (!positionValid) return baseColor;
    float3 dx = dfdx(viewPosition);
    float3 dy = dfdy(viewPosition);
    float3 face = cross(dx, dy);
    float magnitude = length(face);
    if (!isfinite(magnitude) || magnitude < 1.0e-6) return baseColor;
    float3 normal = face / magnitude;
    float3 normalColor = 0.5 + 0.5 * normal;
    return float4(mix(baseColor.rgb, normalColor, clamp(params.x, 0.0, 1.0)), baseColor.a);
}
