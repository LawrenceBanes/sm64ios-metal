#include <metal_stdlib>
using namespace metal;

/* Flat diffuse lighting from true view-space positions. Orient the derivative
 * face toward the camera so reversed triangle winding does not flip lighting.
 * This is not a recovered smooth vertex normal. */
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
    float3 toCamera = -viewPosition;
    float cameraLength = length(toCamera);
    if (!isfinite(cameraLength) || cameraLength < 1.0e-6) return baseColor;
    if (dot(normal, toCamera / cameraLength) < 0.0) normal = -normal;
    /* Both oriented face normal and configured sun are in view space. */
    float sunLength = length(sunDirectionView);
    float ambient = isfinite(params.y) ? clamp(params.y, 0.0, 1.0) : 0.22;
    float directStrength = isfinite(params.x) ? clamp(params.x, 0.0, 2.0) : 1.0;
    if (!isfinite(sunLength) || sunLength < 1.0e-6 || !isfinite(sunIntensity))
        return float4(baseColor.rgb * ambient, baseColor.a);
    float diffuse = max(dot(normal, sunDirectionView / sunLength), 0.0);
    float direct = diffuse * max(sunIntensity, 0.0) * directStrength;
    float illumination = clamp(ambient + direct, 0.0, 2.0);
    return float4(baseColor.rgb * illumination, baseColor.a);
}
