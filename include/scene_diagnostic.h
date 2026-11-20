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
#define SCENE_EXTERNAL_PACKAGE_MARKET_DAY 4101
#define SCENE_EXTERNAL_PACKAGE_MIN 4100
#define SCENE_EXTERNAL_PACKAGE_MAX 4199
#define SCENE_EXTERNAL_RETURN_TO_CASTLE 0
#define SCENE_EXTERNAL_RETURN_TO_PREVIOUS 0xFFFFFFFFu
#define SCENE_DIAGNOSTIC_INVALID_PACKAGE 4294967294
#define SCENE_DIAGNOSTIC_DIRECTORY "scene-diagnostic/"

enum SceneDiagnosticRequest {
    SCENE_DIAGNOSTIC_TOGGLE_AREA = 1,
    SCENE_DIAGNOSTIC_CHECK_FAILED_LOAD,
    SCENE_DIAGNOSTIC_TOGGLE_WSC3,
    SCENE_DIAGNOSTIC_TOGGLE_EXTERNAL,
};

void scene_diagnostic_request(enum SceneDiagnosticRequest request);
void scene_diagnostic_request_external(u32 package_id);
void scene_diagnostic_check_failed_load(void);
bool area_external_scene_available(u32 package_id);
bool area_external_scene_select(u32 package_id);
bool area_external_scene_is_active(void);
u32 area_external_scene_previous_id(void);
bool area_external_scene_spawn(struct SpawnInfo *spawn);
void area_external_scene_track(const f32 *position);
#endif

#endif
