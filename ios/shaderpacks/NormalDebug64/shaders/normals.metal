#include <metal_stdlib>
using namespace metal;

float4 shader64_pass_normals(float4 color, float2 uv, texture2d<float> image,
                             sampler imageSampler, texture2d<float> sceneNormals,
                             sampler normalSampler, constant float4 &params) {
    (void)color;
    (void)image;
    (void)imageSampler;
    (void)params;
    float4 normal = sceneNormals.sample(normalSampler, uv);
    if (normal.a < 0.5) return float4(0.0, 0.0, 0.0, 1.0);
    return float4(normal.rgb, 1.0);
}
