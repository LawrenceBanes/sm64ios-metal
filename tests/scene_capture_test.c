#include "../src/pc/gfx/gfx_scene_capture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct GfxSceneCaptureTriangle triangle;
    memset(&triangle, 0, sizeof(triangle));
    triangle.geometry_id = 101;
    triangle.instance_id = 202;
    triangle.combiner_id = 0x01234567;
    triangle.texture_id[0] = 9;
    triangle.sampler_state[0] = 0x00010203;
    triangle.vertices[1].position[2] = 17.0f;
    triangle.modelview[3][0] = 12.0f;
    triangle.projection[2][2] = 1.0f;

    gfx_scene_capture_set_enabled(false);
    gfx_scene_capture_begin_frame();
    assert(!gfx_scene_capture_submit(&triangle));
    assert(gfx_scene_capture_get_frame().triangle_count == 0);

    gfx_scene_capture_set_enabled(true);
    gfx_scene_capture_begin_frame();
    assert(gfx_scene_capture_submit(&triangle));
    struct GfxSceneCaptureFrame frame = gfx_scene_capture_get_frame();
    assert(frame.triangle_count == 1);
    assert(frame.triangles[0].geometry_id == 101);
    assert(frame.triangles[0].instance_id == 202);
    assert(frame.triangles[0].texture_id[0] == 9);
    assert(frame.triangles[0].sampler_state[0] == 0x00010203);
    assert(frame.triangles[0].vertices[1].position[2] == 17.0f);
    assert(frame.triangles[0].modelview[3][0] == 12.0f);
    assert(frame.triangles[0].projection[2][2] == 1.0f);
    for (int i = 0; i < 300; ++i) assert(gfx_scene_capture_submit(&triangle));
    frame = gfx_scene_capture_get_frame();
    assert(frame.triangle_count == 301);
    assert(frame.triangles[300].combiner_id == 0x01234567);
    gfx_scene_capture_begin_frame();
    assert(gfx_scene_capture_get_frame().triangle_count == 0);
    gfx_scene_capture_set_enabled(false);
    assert(gfx_scene_capture_hash_combine(8, 0x1234) == gfx_scene_capture_hash_combine(8, 0x1234));
    assert(gfx_scene_capture_hash_combine(8, 0x1234) != gfx_scene_capture_hash_combine(9, 0x1234));
    puts("Scene capture storage checks passed");
    return 0;
}
