#include <metal_stdlib>
using namespace metal;

/* API adaptation of Bliss's optional CAMERA_GRIDLINES final-stage effect.
 * Grid placement and nominal thickness follow the inspected Bliss final.fsh;
 * its hideGUI switch is represented by this pack's explicit enabled control. */
float4 shader64_pass(float4 color, float2 uv, texture2d<float> image,
                     sampler imageSampler, constant float4 &params) {
    if (params.x < 0.5) return color;
    float aspect = float(image.get_width()) / max(float(image.get_height()), 1.0);
    float thicknessY = 0.001;
    float thicknessX = thicknessY / max(aspect, 0.001);
    float xDistance = min(abs(uv.x - 0.33), abs(uv.x - 0.66));
    float yDistance = min(abs(uv.y - 0.33), abs(uv.y - 0.66));
    bool line = xDistance < thicknessX || yDistance < thicknessY;
    return line ? float4(1.0, 1.0, 1.0, color.a) : color;
}
