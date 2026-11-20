#include <metal_stdlib>
using namespace metal;

/* x = quantization blend, y = outline threshold in luminance units. */
float4 shader64_post(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    constexpr float3 luma = float3(0.299, 0.587, 0.114);
    float levels = 4.0;
    float3 stepped = floor(color.rgb * levels + 0.5) / levels;
    float3 cartoon = mix(color.rgb, stepped, clamp(params.x, 0.0, 1.0));
    float2 pixel = 1.0 / float2(image.get_width(), image.get_height());
    float left = dot(image.sample(imageSampler, uv - float2(pixel.x, 0.0)).rgb, luma);
    float right = dot(image.sample(imageSampler, uv + float2(pixel.x, 0.0)).rgb, luma);
    float up = dot(image.sample(imageSampler, uv - float2(0.0, pixel.y)).rgb, luma);
    float down = dot(image.sample(imageSampler, uv + float2(0.0, pixel.y)).rgb, luma);
    float edge = abs(left - right) + abs(up - down);
    float outline = smoothstep(max(params.y, 0.001), max(params.y * 2.0, 0.002), edge);
    return float4(cartoon * (1.0 - outline * 0.82), color.a);
}
