#include <metal_stdlib>
using namespace metal;

/* A broad, intentionally material-agnostic showcase response. SM64 exposes
 * the post-combiner color and a view-space position here, but no reliable
 * roughness/metalness or water classification; this must not be mistaken for
 * per-material PBR or a reflection system. */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, float3 viewPosition, bool positionValid,
                         float3 sunDirectionView, float sunIntensity,
                         constant float4 &params) {
    (void)uv;
    (void)screenPosition;
    (void)materialClass;
    (void)params;
    if (!positionValid) return baseColor;
    float3 dx = dfdx(viewPosition);
    float3 dy = dfdy(viewPosition);
    float3 face = cross(dx, dy);
    float faceLength2 = dot(face, face);
    if (!isfinite(faceLength2) || faceLength2 < 1.0e-10) return baseColor;
    float3 normal = face * rsqrt(faceLength2);
    float3 viewRaw = -viewPosition;
    float viewLength2 = dot(viewRaw, viewRaw);
    if (!isfinite(viewLength2) || viewLength2 < 1.0e-8) return baseColor;
    float3 viewDirection = viewRaw * rsqrt(viewLength2);
    if (dot(normal, viewDirection) < 0.0) normal = -normal;

    float sunLength2 = dot(sunDirectionView, sunDirectionView);
    if (!isfinite(sunLength2) || sunLength2 < 1.0e-8 || !isfinite(sunIntensity)) return baseColor;
    float3 lightDirection = sunDirectionView * rsqrt(sunLength2);
    float noL = max(dot(normal, lightDirection), 0.0);
    float wrappedDiffuse = clamp((dot(normal, lightDirection) + 0.28) / 1.28, 0.0, 1.0);
    float skyFactor = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float3 skyAmbient = mix(float3(0.88, 0.91, 0.97), float3(0.98, 1.0, 1.0), skyFactor);
    float showcaseSun = max(sunIntensity, 0.18);
    float direct = clamp(showcaseSun * 1.75, 0.0, 0.7) * wrappedDiffuse;
    float illumination = clamp(0.78 + skyFactor * 0.16 + direct, 0.78, 1.48);

    float3 halfRaw = lightDirection + viewDirection;
    float halfLength2 = dot(halfRaw, halfRaw);
    float broadHighlight = 0.0;
    if (halfLength2 > 1.0e-8 && noL > 0.0) {
        float3 halfDirection = halfRaw * rsqrt(halfLength2);
        broadHighlight = pow(max(dot(normal, halfDirection), 0.0), 30.0) *
                         clamp(showcaseSun * 0.36, 0.0, 0.13);
    }
    float rim = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0) * 0.055;
    float3 rimTint = float3(0.42, 0.63, 0.92) * rim;
    float3 color = baseColor.rgb * illumination * skyAmbient + broadHighlight + rimTint;
    return float4(max(color, float3(0.0)), baseColor.a);
}
