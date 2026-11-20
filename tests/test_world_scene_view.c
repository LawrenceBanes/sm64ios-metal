#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "engine/world_scene_view.h"

static float depth01(float distance, float nearPlane, float farPlane) {
    return farPlane / (farPlane - nearPlane) * (1.0f - nearPlane / distance);
}

static void test_presets_are_scene_scoped(void) {
    assert(world_scene_view_get_distance() == WORLD_SCENE_VIEW_EXTENDED);
    assert(world_scene_view_far_plane(20000.0f, false) == 20000.0f);
    assert(world_scene_view_far_plane(20000.0f, true) == 35000.0f);

    assert(world_scene_view_set_distance(WORLD_SCENE_VIEW_ORIGINAL));
    assert(world_scene_view_far_plane(20000.0f, true) == 20000.0f);
    assert(world_scene_view_far_plane(20000.0f, false) == 20000.0f);

    assert(world_scene_view_set_distance(WORLD_SCENE_VIEW_MAXIMUM));
    assert(world_scene_view_far_plane(20000.0f, true) == 60000.0f);
    assert(world_scene_view_far_plane(64000.0f, true) == 64000.0f);
    assert(!world_scene_view_set_distance((enum WorldSceneViewDistance)99));
}

static void test_objects_remain_within_matrix_range(void) {
    assert(world_scene_view_object_limit(20000.0f, 300.0f) == 20300.0f);
    assert(world_scene_view_object_limit(35000.0f, 300.0f) == 32760.0f);
    assert(world_scene_view_object_limit(60000.0f, 5000.0f) == 32760.0f);
}

static void test_depth_endpoints_and_fog_independence(void) {
    assert(world_scene_view_set_distance(WORLD_SCENE_VIEW_EXTENDED));
    const float nearPlane = 100.0f;
    const float farPlane = world_scene_view_far_plane(20000.0f, true);
    assert(fabsf(depth01(nearPlane, nearPlane, farPlane)) < 1e-6f);
    assert(fabsf(depth01(farPlane, nearPlane, farPlane) - 1.0f) < 1e-6f);
    /* Depth32Float can still distinguish a ten-unit gap at the extended far edge. */
    assert(depth01(farPlane, nearPlane, farPlane)
           - depth01(farPlane - 10.0f, nearPlane, farPlane) > 1.0f / 8388608.0f);

    assert(world_scene_view_get_fog_enabled());
    world_scene_view_set_fog_enabled(false);
    assert(!world_scene_view_get_fog_enabled());
    assert(world_scene_view_get_distance() == WORLD_SCENE_VIEW_EXTENDED);
    world_scene_view_set_fog_enabled(true);
    assert(world_scene_view_get_fog_enabled());
}

int main(void) {
    test_presets_are_scene_scoped();
    test_objects_remain_within_matrix_range();
    test_depth_endpoints_and_fog_independence();
    puts("imported world draw distance, object bounds, depth, and fog tests passed");
    return 0;
}
