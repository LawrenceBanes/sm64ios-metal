#include <metal_stdlib>
using namespace metal;

float4 shader64_pass_normals_textured(float4 color, float2 uv, texture2d<float> image,
                                      sampler imageSampler, texture2d<float> sceneNormals,
                                      sampler normalSampler, texture2d<float> packTexture0,
                                      sampler packSampler0, texture2d<float> packTexture1,
                                      sampler packSampler1, texture2d<float> packTexture2,
                                      sampler packSampler2, texture2d<float> packTexture3,
                                      sampler packSampler3, uint packTextureCount,
                                      constant float4 &params) {
    (void)image;
    (void)imageSampler;
    (void)packTexture1;
    (void)packSampler1;
    (void)packTexture2;
    (void)packSampler2;
    (void)packTexture3;
    (void)packSampler3;
    float4 normal = sceneNormals.sample(normalSampler, uv);
    if (normal.a < 0.5) return float4(0.0, 0.0, 0.0, 1.0);
    float3 textureColor = packTextureCount > 0u ? packTexture0.sample(packSampler0, uv).rgb : float3(1.0);
    return float4(mix(normal.rgb, normal.rgb * textureColor, clamp(params.x, 0.0, 1.0)), 1.0);
}
