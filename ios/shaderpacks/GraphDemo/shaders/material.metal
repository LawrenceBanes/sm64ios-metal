#include <metal_stdlib>
using namespace metal;

/*
 * SHADER64 API 2 material hook.
 * materialClass: 0 unclassified, 1 world, 2 object, 3 character.
 * screenPosition is the Fast3D raster position in clip/screen coordinates;
 * this first hook intentionally does not claim world-space geometry data.
 * params is the pack's four-float parameter vector.
 */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    if (materialClass == 3u) {
        float amount = clamp(params.w, 0.0, 1.0);
        float3 stylized = baseColor.rgb * float3(1.18, 0.88, 0.76);
        baseColor.rgb = mix(baseColor.rgb, stylized, amount);
    }
    return baseColor;
}
