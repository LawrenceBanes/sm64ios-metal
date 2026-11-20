#include "world_scene_view.h"

#include <math.h>

#define WORLD_SCENE_VIEW_EXTENDED_SCALE 1.75f
#define WORLD_SCENE_VIEW_MAXIMUM_FAR 60000.0f
#define WORLD_SCENE_VIEW_OBJECT_MATRIX_LIMIT 32760.0f

static enum WorldSceneViewDistance sDistance = WORLD_SCENE_VIEW_EXTENDED;
static bool sFogEnabled = true;

static enum WorldSceneViewDistance load_distance(void) {
    return __atomic_load_n(&sDistance, __ATOMIC_RELAXED);
}

enum WorldSceneViewDistance world_scene_view_get_distance(void) {
    return load_distance();
}

bool world_scene_view_set_distance(enum WorldSceneViewDistance distance) {
    if (distance < WORLD_SCENE_VIEW_ORIGINAL || distance >= WORLD_SCENE_VIEW_DISTANCE_COUNT)
        return false;
    __atomic_store_n(&sDistance, distance, __ATOMIC_RELAXED);
    return true;
}

float world_scene_view_far_plane(float stockFar, bool importedSceneActive) {
    enum WorldSceneViewDistance distance = load_distance();
    if (!isfinite(stockFar) || stockFar <= 0.0f || !importedSceneActive
        || distance == WORLD_SCENE_VIEW_ORIGINAL) return stockFar;
    if (distance == WORLD_SCENE_VIEW_EXTENDED) {
        float extended = stockFar * WORLD_SCENE_VIEW_EXTENDED_SCALE;
        return isfinite(extended) ? extended : WORLD_SCENE_VIEW_MAXIMUM_FAR;
    }
    return fmaxf(stockFar, WORLD_SCENE_VIEW_MAXIMUM_FAR);
}

float world_scene_view_object_limit(float selectedFar, float cullingRadius) {
    if (!isfinite(selectedFar) || selectedFar <= 0.0f) return 0.0f;
    if (!isfinite(cullingRadius) || cullingRadius < 0.0f) cullingRadius = 0.0f;
    float limit = selectedFar + cullingRadius;
    if (!isfinite(limit) || limit > WORLD_SCENE_VIEW_OBJECT_MATRIX_LIMIT)
        limit = WORLD_SCENE_VIEW_OBJECT_MATRIX_LIMIT;
    return limit;
}

bool world_scene_view_get_fog_enabled(void) {
    return __atomic_load_n(&sFogEnabled, __ATOMIC_RELAXED);
}

void world_scene_view_set_fog_enabled(bool enabled) {
    __atomic_store_n(&sFogEnabled, enabled, __ATOMIC_RELAXED);
}
