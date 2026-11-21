#include <metal_stdlib>
using namespace metal;

/* One cohesive material path: actual Fast3D UVs and the imported pack texture,
 * view-space positions, derivative face normals, and the renderer's configured
 * view-space sun. Faces are oriented toward the camera before diffuse shading,
 * tolerating reversed projected winding in legacy display lists. */
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, float3 viewPosition, bool positionValid,
                         float3 sunDirectionView, float sunIntensity,
                         texture2d<float> materialTexture0, sampler materialSampler0,
                         texture2d<float> materialTexture1, sampler materialSampler1,
                         texture2d<float> materialTexture2, sampler materialSampler2,
                         texture2d<float> materialTexture3, sampler materialSampler3,
                         uint materialTextureCount, constant float4 &params) {
    (void)screenPosition;
    (void)materialClass;
    (void)materialTexture1;
    (void)materialSampler1;
    (void)materialTexture2;
    (void)materialSampler2;
    (void)materialTexture3;
    (void)materialSampler3;

    float textureMix = (materialClass == 3u && isfinite(params.z))
        ? clamp(params.z, 0.0, 1.0) : 0.0;
    float3 albedo = baseColor.rgb;
    if (textureMix > 0.0 && materialTextureCount > 0u) {
        float3 texel = materialTexture0.sample(materialSampler0, fract(uv * 4.0)).rgb;
        albedo = mix(baseColor.rgb, baseColor.rgb * texel, textureMix);
    }
    /* Neutral settings preserve the complete Fast3D combiner/fog result. */
    float ambient = isfinite(params.y) ? clamp(params.y, 0.0, 1.0) : 1.0;
    float directStrength = isfinite(params.x) ? clamp(params.x, 0.0, 2.0) : 0.0;
    if (textureMix <= 0.0 && ambient >= 1.0 && directStrength <= 0.0)
        return baseColor;
    if (!positionValid) return float4(albedo, baseColor.a);

    float3 dx = dfdx(viewPosition);
    float3 dy = dfdy(viewPosition);
    float3 face = cross(dx, dy);
    float faceLength = length(face);
    if (!isfinite(faceLength) || faceLength < 1.0e-6)
        return float4(albedo, baseColor.a);

    float3 sun = sunDirectionView;
    float sunLength = length(sun);
    if (!isfinite(sunLength) || sunLength < 1.0e-6 || !isfinite(sunIntensity))
        return float4(albedo * ambient, baseColor.a);
    float3 normal = face / faceLength;
    float3 toCamera = -viewPosition;
    float cameraLength = length(toCamera);
    if (!isfinite(cameraLength) || cameraLength < 1.0e-6)
        return float4(albedo * ambient, baseColor.a);
    if (dot(normal, toCamera / cameraLength) < 0.0) normal = -normal;
    /* View-facing orientation makes the one-sided diffuse stable for reversed winding. */
    float diffuse = max(dot(normal, sun / sunLength), 0.0);
    float illumination = clamp(ambient + diffuse * max(sunIntensity, 0.0) * directStrength,
                               0.0, 2.0);
    return float4(albedo * illumination, baseColor.a);
}
