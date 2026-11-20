#include <metal_stdlib>
using namespace metal;

struct Shader64In {
    float4 position [[position]];
    float2 uv;
};

vertex Shader64In sm64_shader64_vertex(uint vertexID [[vertex_id]]) {
    Shader64In out;
    out.position = vertexID == 0 ? float4(-1.0, -1.0, 0.0, 1.0) :
                   vertexID == 1 ? float4( 3.0, -1.0, 0.0, 1.0) :
                                   float4(-1.0,  3.0, 0.0, 1.0);
    out.uv = vertexID == 0 ? float2(0.0, 1.0) :
             vertexID == 1 ? float2(2.0, 1.0) : float2(0.0, -1.0);
    return out;
}

float4 shader64_post(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    (void)uv;
    (void)image;
    (void)imageSampler;
    return float4(color.rgb * max(params.x, 0.0), color.a);
}

fragment float4 sm64_shader64_fragment(
    Shader64In in [[stage_in]],
    texture2d<float> image [[texture(0)]],
    sampler imageSampler [[sampler(0)]],
    constant float4 &parameters [[buffer(0)]]) {
    float4 color = image.sample(imageSampler, in.uv);
    return shader64_post(color, in.uv, image, imageSampler, parameters);
}
