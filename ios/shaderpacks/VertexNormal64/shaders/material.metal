#include <metal_stdlib>
using namespace metal;

/* Visual diagnostic for the optional SHADER64 API 2 view-space normal input.
 * Invalid normals preserve the original combiner color unchanged. */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, float3 viewNormal, bool normalValid,
                         constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    (void)materialClass;
    if (!normalValid) return baseColor;
    float3 encoded = normalize(viewNormal) * 0.5 + 0.5;
    return float4(mix(baseColor.rgb, encoded, clamp(params.x, 0.0, 1.0)), baseColor.a);
}
