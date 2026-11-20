#include <metal_stdlib>
using namespace metal;

float4 shader64_post(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    return color;
}
