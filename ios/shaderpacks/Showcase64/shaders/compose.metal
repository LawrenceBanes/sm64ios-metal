#include <metal_stdlib>
using namespace metal;

float4 shader64_pass_scene_color(float4 color, float2 uv, texture2d<float> image,
                                 sampler imageSampler,
                                 texture2d<float> sceneColor [[texture(9)]],
                                 sampler sceneColorSampler [[sampler(9)]],
                                 constant float4 &params) {
    (void)params;
    float4 volume = image.sample(imageSampler, uv);
    float4 source = sceneColor.sample(sceneColorSampler, uv);
    float3 composite = source.rgb * clamp(volume.a, 0.0, 1.0) + volume.rgb;
    return float4(max(composite, float3(0.0)), source.a);
}
