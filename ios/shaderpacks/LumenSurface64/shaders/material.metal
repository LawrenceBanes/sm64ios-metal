#include <metal_stdlib>
using namespace metal;

/* One cohesive material path: actual Fast3D UVs and the imported pack texture,
 * view-space positions, derivative face normals, and the renderer's configured
 * view-space sun. The lighting is intentionally two-sided to tolerate reversed
 * projected winding in legacy display lists. */
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

    float3 texel = float3(1.0);
    if (materialTextureCount > 0u)
        texel = materialTexture0.sample(materialSampler0, fract(uv * 4.0)).rgb;
    float textureMix = isfinite(params.z) ? clamp(params.z, 0.0, 1.0) : 0.0;
    float3 albedo = mix(baseColor.rgb, baseColor.rgb * texel, textureMix);
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
        return float4(albedo * clamp(params.y, 0.0, 1.0), baseColor.a);
    float3 normal = face / faceLength;
    /* abs(dot) makes lighting invariant to derivative orientation reversal. */
    float diffuse = abs(dot(normal, sun / sunLength));
    float ambient = isfinite(params.y) ? clamp(params.y, 0.0, 1.0) : 0.22;
    float directStrength = isfinite(params.x) ? clamp(params.x, 0.0, 2.0) : 1.0;
    float illumination = clamp(ambient + diffuse * max(sunIntensity, 0.0) * directStrength,
                               0.0, 2.0);
    return float4(albedo * illumination, baseColor.a);
}
