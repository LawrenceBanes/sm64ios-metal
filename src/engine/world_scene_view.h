#ifndef WORLD_SCENE_VIEW_H
#define WORLD_SCENE_VIEW_H

#include <stdbool.h>

enum WorldSceneViewDistance {
    WORLD_SCENE_VIEW_ORIGINAL = 0,
    WORLD_SCENE_VIEW_EXTENDED = 1,
    WORLD_SCENE_VIEW_MAXIMUM = 2,
    WORLD_SCENE_VIEW_DISTANCE_COUNT
};

/* The default keeps the 1.75x OoT import visible without changing stock levels. */
enum WorldSceneViewDistance world_scene_view_get_distance(void);
bool world_scene_view_set_distance(enum WorldSceneViewDistance distance);
float world_scene_view_far_plane(float stockFar, bool importedSceneActive);

/* Object transforms are stored in signed 16.16 matrices; cap their center
 * distance before conversion even when static scene geometry uses a farther clip plane. */
float world_scene_view_object_limit(float selectedFar, float cullingRadius);

bool world_scene_view_get_fog_enabled(void);
void world_scene_view_set_fog_enabled(bool enabled);

#endif
