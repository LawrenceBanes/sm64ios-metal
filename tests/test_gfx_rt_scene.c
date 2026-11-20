#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/gfx/gfx_rt_scene.h"

static struct GfxSceneCaptureTriangle triangle(uint64_t instance, float x, bool world) {
    struct GfxSceneCaptureTriangle result = {0};
    result.has_world_transform = world;
    result.instance_id = instance;
    result.object_transform[0][0] = 1.0f;
    result.object_transform[1][1] = 1.0f;
    result.object_transform[2][2] = 1.0f;
    result.object_transform[3][3] = 1.0f;
    result.object_transform[3][0] = x;
    result.vertices[0].position[0] = 0.0f;
    result.vertices[1].position[0] = 1.0f;
    result.vertices[2].position[1] = 1.0f;
    return result;
}

static void test_stable_world_geometry_is_retained(void) {
    gfx_rt_scene_reset();
    struct GfxSceneCaptureTriangle source = triangle(11, 12.0f, true);
    struct GfxSceneCaptureFrame frame = {0};
    frame.triangles = &source;
    frame.triangle_count = 1;
    frame.frame_index = 1;
    frame.has_view_matrix = frame.has_projection_matrix = true;
    frame.view_matrix[0][0] = frame.view_matrix[1][1] = frame.view_matrix[2][2] = frame.view_matrix[3][3] = 1.0f;
    frame.projection_matrix[0][0] = frame.projection_matrix[1][1] = frame.projection_matrix[2][2] = frame.projection_matrix[3][3] = 1.0f;

    struct GfxRTScene scene = gfx_rt_scene_capture_static(&frame);
    assert(scene.triangle_count == 0); /* Wait for a second identical frame. */
    assert(scene.has_camera && scene.source_frame == 1);

    frame.frame_index = 2;
    scene = gfx_rt_scene_capture_static(&frame);
    assert(scene.triangle_count == 1);
    assert(scene.triangles[0].position[0][0] == 12.0f);
    assert(scene.triangles[0].position[1][0] == 13.0f);
    assert(scene.triangles[0].position[2][1] == 1.0f);
    uint64_t stable_hash = scene.geometry_hash;

    frame.frame_index = 3;
    scene = gfx_rt_scene_capture_static(&frame);
    assert(scene.triangle_count == 1 && scene.geometry_hash == stable_hash);
}

static void test_moving_and_invalid_geometry_are_excluded(void) {
    gfx_rt_scene_reset();
    struct GfxSceneCaptureTriangle sources[2] = {
        triangle(21, 0.0f, true), triangle(22, 0.0f, false)
    };
    struct GfxSceneCaptureFrame frame = {0};
    frame.triangles = sources;
    frame.triangle_count = 2;
    (void)gfx_rt_scene_capture_static(&frame);
    frame.frame_index++;
    struct GfxRTScene scene = gfx_rt_scene_capture_static(&frame);
    assert(scene.triangle_count == 1);

    sources[0] = triangle(23, 100.0f, true); /* A changed transform gets a new instance key. */
    frame.triangle_count = 1;
    frame.frame_index++;
    scene = gfx_rt_scene_capture_static(&frame);
    assert(scene.triangle_count == 0);
    assert(scene.geometry_hash == 0);
}

static void test_capture_budget_is_bounded(void) {
    gfx_rt_scene_reset();
    struct GfxSceneCaptureTriangle source = triangle(31, 0.0f, true);
    struct GfxSceneCaptureFrame frame = {0};
    frame.triangles = &source;
    frame.triangle_count = 1;
    (void)gfx_rt_scene_capture_static(&frame);
    frame.triangle_count = GFX_RT_SCENE_MAX_TRIANGLES + 1u;
    struct GfxSceneCaptureTriangle *many = calloc(frame.triangle_count, sizeof(*many));
    assert(many);
    for (size_t i = 0; i < frame.triangle_count; ++i) {
        many[i] = triangle(31, 0.0f, true);
    }
    frame.triangles = many;
    struct GfxRTScene scene = gfx_rt_scene_capture_static(&frame);
    assert(scene.triangle_count == GFX_RT_SCENE_MAX_TRIANGLES);
    assert(scene.dropped_triangles >= 1);
    free(many);

    frame.triangles = &source;
    frame.triangle_count = GFX_RT_SCENE_MAX_CAPTURE_INPUT + 1u;
    scene = gfx_rt_scene_capture_static(&frame);
    assert(scene.triangle_count == 0);
    assert(scene.dropped_triangles == frame.triangle_count);
}

int main(void) {
    test_stable_world_geometry_is_retained();
    test_moving_and_invalid_geometry_are_excluded();
    test_capture_budget_is_bounded();
    puts("Metal RT static-scene capture, stability, transform, and bound tests passed");
    return 0;
}
