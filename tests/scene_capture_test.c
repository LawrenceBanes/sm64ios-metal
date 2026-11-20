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
    triangle.object_transform[3][0] = 2.0f;
    triangle.has_world_transform = true;

    float view[4][4] = {{0}};
    float updated_view[4][4] = {{0}};
    float projection[4][4] = {{0}};
    for (int i = 0; i < 4; ++i) view[i][i] = updated_view[i][i] = projection[i][i] = 1.0f;
    view[3][0] = -10.0f;
    updated_view[3][0] = -10.0f;
    projection[2][2] = 0.5f;

    gfx_scene_capture_set_enabled(false);
    gfx_scene_capture_begin_frame();
    assert(!gfx_scene_capture_submit(&triangle));
    assert(gfx_scene_capture_get_frame().triangle_count == 0);

    gfx_scene_capture_set_enabled(true);
    gfx_scene_capture_set_geometry_enabled(false);
    gfx_scene_capture_begin_frame();
    gfx_scene_capture_set_view_matrix(view);
    gfx_scene_capture_set_projection_matrix(projection);
    assert(!gfx_scene_capture_submit(&triangle));
    struct GfxSceneCaptureFrame matrix_only_frame = gfx_scene_capture_get_frame();
    assert(matrix_only_frame.has_view_matrix && matrix_only_frame.has_projection_matrix);
    assert(matrix_only_frame.triangle_count == 0);
    gfx_scene_capture_set_geometry_enabled(true);
    gfx_scene_capture_set_view_matrix(updated_view);
    assert(gfx_scene_capture_submit(&triangle));
    struct GfxSceneCaptureFrame frame = gfx_scene_capture_get_frame();
    assert(frame.triangle_count == 1);
    assert(frame.has_view_matrix && frame.has_projection_matrix);
    assert(frame.view_matrix[3][0] == -10.0f);
    assert(frame.projection_matrix[2][2] == 0.5f);
    assert(frame.triangles[0].geometry_id == 101);
    assert(frame.triangles[0].instance_id == 202);
    assert(frame.triangles[0].texture_id[0] == 9);
    assert(frame.triangles[0].sampler_state[0] == 0x00010203);
    assert(frame.triangles[0].vertices[1].position[2] == 17.0f);
    assert(frame.triangles[0].has_world_transform);
    assert(frame.triangles[0].object_transform[3][0] == 2.0f);
    for (int i = 0; i < 300; ++i) assert(gfx_scene_capture_submit(&triangle));
    frame = gfx_scene_capture_get_frame();
    assert(frame.triangle_count == 301);
    assert(frame.triangles[300].combiner_id == 0x01234567);
    float changed_view[4][4];
    memcpy(changed_view, view, sizeof(changed_view));
    changed_view[3][0] = -9.0f;
    gfx_scene_capture_set_view_matrix(changed_view);
    frame = gfx_scene_capture_get_frame();
    assert(!frame.has_view_matrix);
    assert(!frame.triangles[0].has_world_transform);
    gfx_scene_capture_begin_frame();
    assert(gfx_scene_capture_get_frame().triangle_count == 0);
    assert(!gfx_scene_capture_get_frame().has_view_matrix);
    assert(!gfx_scene_capture_get_frame().has_projection_matrix);
    gfx_scene_capture_set_enabled(false);
    assert(gfx_scene_capture_hash_combine(8, 0x1234) == gfx_scene_capture_hash_combine(8, 0x1234));
    assert(gfx_scene_capture_hash_combine(8, 0x1234) != gfx_scene_capture_hash_combine(9, 0x1234));
    const uint32_t geometry_a[] = {1, 2, 3, 4};
    const uint32_t geometry_b[] = {1, 2, 3, 4};
    const uint32_t geometry_changed[] = {1, 2, 3, 5};
    uint64_t stable_key_a = gfx_scene_capture_hash_bytes(0, geometry_a, sizeof(geometry_a));
    uint64_t stable_key_b = gfx_scene_capture_hash_bytes(0, geometry_b, sizeof(geometry_b));
    uint64_t changed_key = gfx_scene_capture_hash_bytes(0, geometry_changed, sizeof(geometry_changed));
    assert(stable_key_a == stable_key_b);
    assert(stable_key_a != changed_key);

    float combined_modelview[4][4] = {{0}};
    float split_transform[4][4] = {{0}};
    for (int i = 0; i < 4; ++i) combined_modelview[i][i] = 1.0f;
    combined_modelview[3][0] = -8.0f; /* object translation 2 followed by camera translation -10 */
    assert(gfx_scene_capture_split_modelview(combined_modelview, view, split_transform));
    assert(split_transform[3][0] == 2.0f);
    float singular_view[4][4] = {{0}};
    assert(!gfx_scene_capture_split_modelview(combined_modelview, singular_view, split_transform));
    gfx_scene_capture_set_enabled(true);
    gfx_scene_capture_set_triangle_limit(2);
    gfx_scene_capture_begin_frame();
    assert(gfx_scene_capture_submit(&triangle));
    assert(gfx_scene_capture_submit(&triangle));
    assert(!gfx_scene_capture_submit(&triangle));
    frame = gfx_scene_capture_get_frame();
    assert(frame.triangle_count == 2 && frame.dropped_triangles == 1);
    gfx_scene_capture_set_triangle_limit(0);
    gfx_scene_capture_set_enabled(false);
    puts("Scene capture storage checks passed");
    return 0;
}
