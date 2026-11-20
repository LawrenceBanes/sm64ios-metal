#include <metal_stdlib>
using namespace metal;

/* Visualize a geometric face normal derived from true view-space positions.
 * This is a flat, screen-space derivative normal, not a recovered smooth normal. */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, float3 viewPosition, bool positionValid,
                         float3 sunDirectionView, float sunIntensity,
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
    /* Use two-sided diffuse lighting because the public hook does not receive
     * Fast3D front/back winding as an explicit input. */
    float sunLength = length(sunDirectionView);
    if (!isfinite(sunLength) || sunLength < 1.0e-6 || !isfinite(sunIntensity)) return baseColor;
    float direct = abs(dot(normal, sunDirectionView / sunLength)) * max(sunIntensity, 0.0);
    float illumination = clamp(0.22 + direct, 0.0, 1.5);
    return float4(baseColor.rgb * mix(1.0, illumination, clamp(params.x, 0.0, 1.0)), baseColor.a);
}
