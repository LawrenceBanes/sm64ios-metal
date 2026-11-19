#ifndef SCENE_DIAGNOSTIC_H
#define SCENE_DIAGNOSTIC_H

#ifdef IOS_SCENE_DIAGNOSTIC
#include <stdbool.h>
#include <PR/ultratypes.h>
struct SpawnInfo;
#define SCENE_DIAGNOSTIC_AREA 2
#define SCENE_DIAGNOSTIC_NODE 0x70
#define SCENE_DIAGNOSTIC_PACKAGE 4098
#define SCENE_DIAGNOSTIC_WSC3_AREA 3
#define SCENE_DIAGNOSTIC_WSC3_PACKAGE 4099
#define SCENE_EXTERNAL_AREA 4
#define SCENE_EXTERNAL_PACKAGE 4100
#define SCENE_DIAGNOSTIC_INVALID_PACKAGE 4294967294
#define SCENE_DIAGNOSTIC_DIRECTORY "scene-diagnostic/"

enum SceneDiagnosticRequest {
    SCENE_DIAGNOSTIC_TOGGLE_AREA = 1,
    SCENE_DIAGNOSTIC_CHECK_FAILED_LOAD,
    SCENE_DIAGNOSTIC_TOGGLE_WSC3,
    SCENE_DIAGNOSTIC_TOGGLE_EXTERNAL,
};

void scene_diagnostic_request(enum SceneDiagnosticRequest request);
void scene_diagnostic_check_failed_load(void);
bool area_external_scene_available(void);
bool area_external_scene_spawn(struct SpawnInfo *spawn);
void area_external_scene_track(const f32 *position);
#endif

#endif
