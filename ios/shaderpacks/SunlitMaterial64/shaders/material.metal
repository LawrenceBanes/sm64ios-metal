#include <metal_stdlib>
using namespace metal;

/* Geometry-aware material lighting using source SM64 normals and the engine's
 * active directional light transformed into the same view-space basis. */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, float3 viewNormal, bool normalValid,
                         float3 sunDirectionView, float sunIntensity,
                         constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    (void)materialClass;
    if (!normalValid) return baseColor;
    float nDotL = max(dot(normalize(viewNormal), normalize(sunDirectionView)), 0.0);
    float direct = nDotL * sunIntensity * clamp(params.x, 0.0, 2.0);
    float illumination = clamp(0.32 + direct, 0.0, 1.5);
    return float4(baseColor.rgb * illumination, baseColor.a);
}
