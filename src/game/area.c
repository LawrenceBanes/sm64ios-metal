#include <stdbool.h>
#include <PR/ultratypes.h>

#include "prevent_bss_reordering.h"
#include "area.h"
#include "sm64.h"
#include "gfx_dimensions.h"
#include "behavior_data.h"
#include "game_init.h"
#include "object_list_processor.h"
#include "engine/surface_load.h"
#include "scene_diagnostic.h"
#ifdef IOS_SCENE_DIAGNOSTIC
#include "engine/surface_collision.h"
#include "surface_terrains.h"
#include "pc/fs/fs.h"
#include <math.h>
#endif
#if defined(IOS_CONNECTED_WORLD_TEST)
#include "engine/world_region.h"
#endif
#ifdef IOS_O2R_SCENE_RUNTIME
#include "engine/world_scene_file.h"
#endif
#include "ingame_menu.h"
#include "screen_transition.h"
#include "mario.h"
#include "mario_hat.h"
#include "mario_actions_cutscene.h"
#include "print.h"
#include "hud.h"
#include "audio/external.h"
#include "area.h"
#include "rendering_graph_node.h"
#include "level_update.h"
#include "engine/geo_layout.h"
#include "save_file.h"
#include "level_table.h"
#if defined(TARGET_OS_IOS) && defined(VERSION_US)
#include "ios_app_branding.h"
#include "segment2.h"
static bool sShowIntroBranding;
#endif
#ifdef IOS_BUILD
#include "pc/gfx/gfx_ios_diagnostics.h"
#endif

#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD)
#define IOS_HYRULE_DIAG(...) gfx_ios_diag_log(__VA_ARGS__)
#else
#define IOS_HYRULE_DIAG(...) ((void)0)
#endif

#if defined(IOS_BUILD) && defined(IOS_CONNECTED_WORLD_TEST)
#define IOS_CONNECTED_WORLD_DIAG(...) gfx_ios_diag_log(__VA_ARGS__)
#else
#define IOS_CONNECTED_WORLD_DIAG(...) ((void)0)
#endif

#include "gfx_dimensions.h"

struct SpawnInfo gPlayerSpawnInfos[1];
struct GraphNode *D_8033A160[0x100];
struct Area gAreaData[8];

struct WarpTransition gWarpTransition;

s16 gCurrCourseNum;
s16 gCurrActNum;
s16 gCurrAreaIndex;
s16 gSavedCourseNum;
s16 gPauseScreenMode;
s16 gSaveOptSelectIndex;

struct SpawnInfo *gMarioSpawnInfo = &gPlayerSpawnInfos[0];
struct GraphNode **gLoadedGraphNodes = D_8033A160;
struct Area *gAreas = gAreaData;
struct Area *gCurrentArea = NULL;
#ifdef IOS_O2R_SCENE_RUNTIME
static struct WorldSceneRuntime sAreaScene;

#ifdef IOS_SCENE_DIAGNOSTIC
_Static_assert(SCENE_DIAGNOSTIC_PACKAGE == (LEVEL_CASTLE_GROUNDS << 8 | SCENE_DIAGNOSTIC_AREA),
               "WSC2 diagnostic package ID must match the Castle Grounds area key");
_Static_assert(SCENE_DIAGNOSTIC_WSC3_PACKAGE == (LEVEL_CASTLE_GROUNDS << 8 | SCENE_DIAGNOSTIC_WSC3_AREA),
               "WSC3 diagnostic package ID must match the Castle Grounds area key");
_Static_assert(SCENE_EXTERNAL_PACKAGE == (LEVEL_CASTLE_GROUNDS << 8 | SCENE_EXTERNAL_AREA),
               "External package ID must match its reserved Castle Grounds area");
static struct WorldSceneDestination sExternalSpawn;
#if defined(IOS_BUILD)
#define SCENE_DIAG_LOG(...) gfx_ios_diag_log(__VA_ARGS__)
#else
#define SCENE_DIAG_LOG(...) ((void)0)
#endif

bool area_external_scene_available(void) {
    fs_scene_library_reset();
    void *bytes = fs_load_scene_resource("scenes/4100.wsc", 16 * 1024 * 1024, NULL);
    bool available = bytes != NULL;
    free(bytes);
    return available;
}

bool area_external_scene_spawn(struct SpawnInfo *spawn) {
    if (area_scene_package_id() != SCENE_EXTERNAL_PACKAGE) return false;
    if (spawn != NULL) {
        spawn->startPos[0] = sExternalSpawn.position.x;
        spawn->startPos[1] = sExternalSpawn.position.y;
        spawn->startPos[2] = sExternalSpawn.position.z;
        spawn->startAngle[1] = sExternalSpawn.yaw;
    }
    return true;
}

static bool external_spawn_has_clearance(struct WorldPoint point) {
    static const s16 samples[][2] = {
        {0, 0}, {-50, 0}, {50, 0}, {0, -50}, {0, 50},
        {-35, -35}, {-35, 35}, {35, -35}, {35, 35},
    };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        f32 x = point.x + samples[i][0], z = point.z + samples[i][1];
        struct Surface *floor, *ceil;
        f32 height = find_floor(x, point.y, z, &floor);
        if (!floor || floor->type != SURFACE_DEFAULT || floor->normal.y < 0.707f
            || point.y - height > 80 || height - point.y > 35 || height < -10000) {
            SCENE_DIAG_LOG("Hyrule WSC5 spawn clearance failed sample=%zu point=(%.1f,%.1f,%.1f) floor=%s height=%.1f normalY=%.3f type=%d",
                           i, x, point.y, z, floor ? "hit" : "miss", height,
                           floor ? floor->normal.y : 0.0f, floor ? floor->type : -1);
            return false;
        }
        f32 top = find_ceil(x, height, z, &ceil);
        if (ceil && top < height + 180) {
            SCENE_DIAG_LOG("Hyrule WSC5 spawn clearance failed: low ceiling sample=%zu point=(%.1f,%.1f,%.1f) ceiling=%.1f",
                           i, x, point.y, z, top);
            return false;
        }
    }
    f32 x = point.x, y = point.y, z = point.z;
    if (f32_find_wall_collision(&x, &y, &z, 30, 50) != 0) {
        SCENE_DIAG_LOG("Hyrule WSC5 spawn clearance failed: near wall point=(%.1f,%.1f,%.1f)",
                       point.x, point.y, point.z);
        return false;
    }
    x = point.x;
    z = point.z;
    if (f32_find_wall_collision(&x, &y, &z, 150, 50) != 0) {
        SCENE_DIAG_LOG("Hyrule WSC5 spawn clearance failed: blocked 150-unit approach point=(%.1f,%.1f,%.1f)",
                       point.x, point.y, point.z);
        return false;
    }
    return true;
}

void area_external_scene_track(const f32 *position) {
    if (area_scene_package_id() != SCENE_EXTERNAL_PACKAGE
        || !isfinite(position[0]) || !isfinite(position[1]) || !isfinite(position[2])
        || position[0] < -32768 || position[0] > 32767
        || position[1] < -32768 || position[1] > 32767
        || position[2] < -32768 || position[2] > 32767) return;
    struct WorldPoint player = {(s32)position[0], (s32)position[1], (s32)position[2]};
    u32 previous = world_scene_current_room(&sAreaScene);
    /* Keep this bounded package resident. Collision and rendering stay
     * available even when a movement step can cross a narrow room. */
    world_scene_track_room(&sAreaScene, player);
    u32 current = world_scene_current_room(&sAreaScene);
    if (current != previous) SCENE_DIAG_LOG("Hyrule WSC5 room %u -> %u", previous, current);
}

void scene_diagnostic_check_failed_load(void) {
    if (area_scene_package_id() != SCENE_DIAGNOSTIC_PACKAGE) {
        SCENE_DIAG_LOG("WSC2 failure probe skipped: enter the diagnostic room first");
        return;
    }
    u32 room = world_scene_current_room(&sAreaScene);
    u32 roomCount = world_scene_active_room_count(&sAreaScene);
    struct Surface *before, *after;
    f32 height = find_floor(0, 40, 720, &before);
    enum WorldSceneResult result = world_scene_transition(&sAreaScene, SCENE_DIAGNOSTIC_INVALID_PACKAGE, 0, NULL);
    f32 newHeight = find_floor(0, 40, 720, &after);
    bool preserved = area_scene_package_id() == SCENE_DIAGNOSTIC_PACKAGE
        && world_scene_current_room(&sAreaScene) == room
        && world_scene_active_room_count(&sAreaScene) == roomCount
        && before != NULL && before == after && height == 0 && newHeight == height;
    SCENE_DIAG_LOG("WSC2 failure probe %s result=%d room=%u floor=%.1f->%.1f",
                   result == WORLD_SCENE_PROVIDER_FAILED && preserved ? "PASS" : "FAIL",
                   result, room, height, newHeight);
}
#endif

u32 area_scene_package_id(void) {
    return world_scene_active_id(&sAreaScene);
}

static void load_area_scene(s32 index) {
    /* Namespaced by stock level and area so saves and level tables keep their
     * original IDs. Entry 0 is the package's default room. */
    u32 sceneId = ((u32)gCurrLevelNum << 8) | (u32)index;
    world_scene_runtime_init(&sAreaScene, &gWorldSceneFileProvider, NULL);
    struct WorldSceneDestination destination = {0};
    enum WorldSceneResult result = world_scene_open(&sAreaScene, sceneId, 0, &destination);
#ifdef IOS_SCENE_DIAGNOSTIC
    if (sceneId == SCENE_EXTERNAL_PACKAGE) {
        if (result == WORLD_SCENE_OK)
            result = world_scene_update_residency(&sAreaScene, destination.position, UINT32_MAX, 0);
        if (result == WORLD_SCENE_OK && !external_spawn_has_clearance(destination.position))
            result = WORLD_SCENE_BAD_PACKAGE;
        if (result == WORLD_SCENE_OK) sExternalSpawn = destination;
        else world_scene_close(&sAreaScene);
        SCENE_DIAG_LOG("Hyrule WSC5 activation result=%d room=%u activeRooms=%u spawn=(%lld,%lld,%lld) yaw=%d",
                       result, world_scene_current_room(&sAreaScene),
                       world_scene_active_room_count(&sAreaScene),
                       (long long)destination.position.x, (long long)destination.position.y,
                       (long long)destination.position.z, destination.yaw);
    }
    if (sceneId == SCENE_DIAGNOSTIC_PACKAGE || sceneId == SCENE_DIAGNOSTIC_WSC3_PACKAGE) {
        struct Surface *floor;
        f32 height = find_floor(0, 40, 720, &floor);
        SCENE_DIAG_LOG("WSC%u diagnostic entry package=%u result=%d room=%u floor=%s y=%.1f",
                       sceneId == SCENE_DIAGNOSTIC_WSC3_PACKAGE ? 3u : 2u, sceneId, result, world_scene_current_room(&sAreaScene), floor ? "hit" : "miss", height);
    }
#endif
    (void)result;
}
#endif
struct CreditsEntry *gCurrCreditsEntry = NULL;
Vp *D_8032CE74 = NULL;
Vp *D_8032CE78 = NULL;
s16 gWarpTransDelay = 0;
u32 gFBSetColor = 0;
u32 gWarpTransFBSetColor = 0;
u8 gWarpTransRed = 0;
u8 gWarpTransGreen = 0;
u8 gWarpTransBlue = 0;
s16 gCurrSaveFileNum = 1;
s16 gCurrLevelNum = LEVEL_MIN;

/*
 * The following two tables are used in get_mario_spawn_type() to determine spawn type
 * from warp behavior.
 * When looping through sWarpBhvSpawnTable, if the behavior function in the table matches
 * the spawn behavior executed, the index of that behavior is used with sSpawnTypeFromWarpBhv
*/

// D_8032CE9C
const BehaviorScript *sWarpBhvSpawnTable[] = {
    bhvDoorWarp,                bhvStar,                   bhvExitPodiumWarp,          bhvWarp,
    bhvWarpPipe,                bhvFadingWarp,             bhvInstantActiveWarp,       bhvAirborneWarp,
    bhvHardAirKnockBackWarp,    bhvSpinAirborneCircleWarp, bhvDeathWarp,               bhvSpinAirborneWarp,
    bhvFlyingWarp,              bhvSwimmingWarp,           bhvPaintingStarCollectWarp, bhvPaintingDeathWarp,
    bhvAirborneStarCollectWarp, bhvAirborneDeathWarp,      bhvLaunchStarCollectWarp,   bhvLaunchDeathWarp,
};

// D_8032CEEC
u8 sSpawnTypeFromWarpBhv[] = {
    MARIO_SPAWN_DOOR_WARP,             MARIO_SPAWN_UNKNOWN_02,           MARIO_SPAWN_UNKNOWN_03,            MARIO_SPAWN_UNKNOWN_03,
    MARIO_SPAWN_UNKNOWN_03,            MARIO_SPAWN_TELEPORT,             MARIO_SPAWN_INSTANT_ACTIVE,        MARIO_SPAWN_AIRBORNE,
    MARIO_SPAWN_HARD_AIR_KNOCKBACK,    MARIO_SPAWN_SPIN_AIRBORNE_CIRCLE, MARIO_SPAWN_DEATH,                 MARIO_SPAWN_SPIN_AIRBORNE,
    MARIO_SPAWN_FLYING,                MARIO_SPAWN_SWIMMING,             MARIO_SPAWN_PAINTING_STAR_COLLECT, MARIO_SPAWN_PAINTING_DEATH,
    MARIO_SPAWN_AIRBORNE_STAR_COLLECT, MARIO_SPAWN_AIRBORNE_DEATH,       MARIO_SPAWN_LAUNCH_STAR_COLLECT,   MARIO_SPAWN_LAUNCH_DEATH,
};

Vp D_8032CF00 = { {
    { 640, 480, 511, 0 },
    { 640, 480, 511, 0 },
} };

#ifdef VERSION_EU
const char *gNoControllerMsg[] = {
    "NO CONTROLLER",
    "MANETTE DEBRANCHEE",
    "CONTROLLER FEHLT",
};
#endif

void override_viewport_and_clip(Vp *a, Vp *b, u8 c, u8 d, u8 e) {
    u16 sp6 = ((c >> 3) << 11) | ((d >> 3) << 6) | ((e >> 3) << 1) | 1;

    gFBSetColor = (sp6 << 16) | sp6;
    D_8032CE74 = a;
    D_8032CE78 = b;
}

void set_warp_transition_rgb(u8 red, u8 green, u8 blue) {
    u16 warpTransitionRGBA16 = ((red >> 3) << 11) | ((green >> 3) << 6) | ((blue >> 3) << 1) | 1;

    gWarpTransFBSetColor = (warpTransitionRGBA16 << 16) | warpTransitionRGBA16;
    gWarpTransRed = red;
    gWarpTransGreen = green;
    gWarpTransBlue = blue;
}

static int scale_x_to_correct_aspect_center(int x) {
    f32 aspect = GFX_DIMENSIONS_ASPECT_RATIO;
    return x + (SCREEN_HEIGHT * aspect / 2) - (SCREEN_WIDTH / 2);
}

void print_intro_text(void) {
#if defined(TARGET_OS_IOS) && defined(VERSION_US)
    sShowIntroBranding = true;
#endif
#ifdef VERSION_EU
    int language = eu_get_language();
#endif
    if ((gGlobalTimer & 0x1F) < 20) {
        if (gControllerBits == 0) {
#ifdef VERSION_EU
            print_text_centered(SCREEN_WIDTH / 2, 20, gNoControllerMsg[language]);
#else
            print_text_centered(scale_x_to_correct_aspect_center(SCREEN_WIDTH / 2), 20, "NO CONTROLLER");
#endif
        } else {
#ifdef VERSION_EU
            print_text(20, 20, "START");
#else
            print_text_centered(60, 38, "PRESS");
            print_text_centered(60, 20, "START");
#endif
        }
    }
}

u32 get_mario_spawn_type(struct Object *o) {
    s32 i;
    const BehaviorScript *behavior = virtual_to_segmented(0x13, o->behavior);

    for (i = 0; i < 20; i++) {
        if (sWarpBhvSpawnTable[i] == behavior) {
            return sSpawnTypeFromWarpBhv[i];
        }
    }
    return 0;
}

struct ObjectWarpNode *area_get_warp_node(u8 id) {
    struct ObjectWarpNode *node = NULL;

    for (node = gCurrentArea->warpNodes; node != NULL; node = node->next) {
        if (node->node.id == id) {
            break;
        }
    }
    return node;
}

struct ObjectWarpNode *area_get_warp_node_from_params(struct Object *o) {
    u8 sp1F = (o->oBehParams & 0x00FF0000) >> 16;

    return area_get_warp_node(sp1F);
}

void load_obj_warp_nodes(void) {
    struct ObjectWarpNode *sp24;
    struct Object *sp20 = (struct Object *) gObjParentGraphNode.children;

    do {
        struct Object *sp1C = sp20;

        if (sp1C->activeFlags != ACTIVE_FLAG_DEACTIVATED && get_mario_spawn_type(sp1C) != 0) {
            sp24 = area_get_warp_node_from_params(sp1C);
            if (sp24 != NULL) {
                sp24->object = sp1C;
            }
        }
    } while ((sp20 = (struct Object *) sp20->header.gfx.node.next)
             != (struct Object *) gObjParentGraphNode.children);
}

void clear_areas(void) {
    s32 i;

#ifdef IOS_O2R_SCENE_RUNTIME
    world_scene_close(&sAreaScene);
#endif
#ifdef IOS_HAT_MOVEMENT
    mario_hat_reset();
#endif

#if defined(IOS_CONNECTED_WORLD_TEST)
    {
        struct WorldPoint origin = {0, 0, 0};
        world_region_reset(origin);
    }
#endif

    gCurrentArea = NULL;
    gWarpTransition.isActive = FALSE;
    gWarpTransition.pauseRendering = FALSE;
    gMarioSpawnInfo->areaIndex = -1;

    for (i = 0; i < 8; i++) {
        gAreaData[i].index = i;
        gAreaData[i].flags = 0;
        gAreaData[i].terrainType = 0;
        gAreaData[i].unk04 = NULL;
        gAreaData[i].terrainData = NULL;
        gAreaData[i].surfaceRooms = NULL;
        gAreaData[i].macroObjects = NULL;
        gAreaData[i].warpNodes = NULL;
        gAreaData[i].paintingWarpNodes = NULL;
        gAreaData[i].instantWarps = NULL;
        gAreaData[i].objectSpawnInfos = NULL;
        gAreaData[i].camera = NULL;
        gAreaData[i].unused28 = NULL;
        gAreaData[i].whirlpools[0] = NULL;
        gAreaData[i].whirlpools[1] = NULL;
        gAreaData[i].dialog[0] = 255;
        gAreaData[i].dialog[1] = 255;
        gAreaData[i].musicParam = 0;
        gAreaData[i].musicParam2 = 0;
    }
}

void clear_area_graph_nodes(void) {
    s32 i;
    if (gCurrentArea != NULL) {
        geo_call_global_function_nodes(&gCurrentArea->unk04->node, GEO_CONTEXT_AREA_UNLOAD);
#ifdef IOS_O2R_SCENE_RUNTIME
        world_scene_close(&sAreaScene);
#endif
        gCurrentArea = NULL;
        gWarpTransition.isActive = FALSE;
    }

    for (i = 0; i < 8; i++) {
        if (gAreaData[i].unk04 != NULL) {
            geo_call_global_function_nodes(&gAreaData[i].unk04->node, GEO_CONTEXT_AREA_INIT);
            gAreaData[i].unk04 = NULL;
        }
    }
}

void load_area(s32 index) {
    if (gCurrentArea == NULL && gAreaData[index].unk04 != NULL) {
        gCurrentArea = &gAreaData[index];
        gCurrAreaIndex = gCurrentArea->index;

        if (gCurrentArea->terrainData != NULL) {
#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD)
            if (gCurrLevelNum == LEVEL_HYRULE_FIELD) {
                IOS_HYRULE_DIAG("Hyrule collision load begin level=%d area=%d terrain=%p rooms=%p",
                                gCurrLevelNum, index, (void *)gCurrentArea->terrainData,
                                (void *)gCurrentArea->surfaceRooms);
            }
#endif
            load_area_terrain(index, gCurrentArea->terrainData, gCurrentArea->surfaceRooms,
                              gCurrentArea->macroObjects);
#if defined(IOS_CONNECTED_WORLD)
            if (gCurrLevelNum == LEVEL_CASTLE_GROUNDS && index == 1) {
                IOS_HYRULE_DIAG(
                    "Castle and Hyrule collision loaded level=%d area=%d surfaces=%d nodes=%d rejected=%d",
                    gCurrLevelNum, index, gNumStaticSurfaces, gNumStaticSurfaceNodes,
                    gSurfaceLoadRejectedCount);
            }
#endif
#if defined(IOS_BUILD) && defined(IOS_CONNECTED_WORLD_TEST)
            if (gCurrLevelNum == LEVEL_CASTLE_GROUNDS) {
                IOS_CONNECTED_WORLD_DIAG(
                    "Connected forest Castle area collision loaded level=%d area=%d surfaces=%d nodes=%d grid=%dx%d bound=%d rejected=%d",
                    gCurrLevelNum, index, gNumStaticSurfaces, gNumStaticSurfaceNodes,
                    SURFACE_PARTITION_DIM, SURFACE_PARTITION_DIM, LEVEL_BOUNDARY_MAX,
                    gSurfaceLoadRejectedCount);
            }
#endif
#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD)
            if (gCurrLevelNum == LEVEL_HYRULE_FIELD) {
                IOS_HYRULE_DIAG("Hyrule collision load complete level=%d area=%d staticSurfaces=%d staticNodes=%d",
                                gCurrLevelNum, index, gNumStaticSurfaces,
                                gNumStaticSurfaceNodes);
            }
#endif
#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD)
        } else if (gCurrLevelNum == LEVEL_HYRULE_FIELD) {
            IOS_HYRULE_DIAG("Hyrule collision load failed level=%d area=%d reason=terrainData-null",
                            gCurrLevelNum, index);
#endif
        }

#ifdef IOS_O2R_SCENE_RUNTIME
        if (gCurrentArea->terrainData != NULL) load_area_scene(index);
#endif
        if (gCurrentArea->objectSpawnInfos != NULL) {
            spawn_objects_from_info(0, gCurrentArea->objectSpawnInfos);
        }

        load_obj_warp_nodes();
        geo_call_global_function_nodes(&gCurrentArea->unk04->node, GEO_CONTEXT_AREA_LOAD);
        IOS_HYRULE_DIAG("SM64 area activated level=%d area=%d terrain=%p spawnInfos=%p",
                        gCurrLevelNum, gCurrentArea->index, (void *)gCurrentArea->terrainData,
                        (void *)gCurrentArea->objectSpawnInfos);
#if defined(IOS_BUILD) && defined(IOS_CONNECTED_WORLD_TEST)
        if (gCurrLevelNum == LEVEL_CASTLE_GROUNDS) {
            IOS_CONNECTED_WORLD_DIAG(
                "Connected forest test active in existing area level=%d area=%d camera=%p; no region transition is installed",
                gCurrLevelNum, gCurrentArea->index, (void *)gCurrentArea->camera);
        }
#endif
    }
}

void unload_area(void) {
#ifdef IOS_HAT_MOVEMENT
    mario_hat_reset();
#endif
    if (gCurrentArea != NULL) {
#if defined(IOS_CONNECTED_WORLD_TEST)
#ifdef IOS_O2R_SCENE_RUNTIME
        if (!area_scene_package_id())
#endif
        {
            struct WorldPoint origin = {0, 0, 0};
            world_region_reset(origin);
        }
#endif
        unload_objects_from_area(0, gCurrentArea->index);
#ifdef IOS_O2R_SCENE_RUNTIME
        if (area_scene_package_id()) {
#ifdef IOS_SCENE_DIAGNOSTIC
            u32 sceneId = area_scene_package_id();
            bool diagnostic = sceneId == SCENE_DIAGNOSTIC_PACKAGE || sceneId == SCENE_DIAGNOSTIC_WSC3_PACKAGE;
#endif
            world_scene_close(&sAreaScene);
#ifdef IOS_SCENE_DIAGNOSTIC
            if (diagnostic) {
                SCENE_DIAG_LOG("WSC%u diagnostic exit activeRooms=%u", sceneId == SCENE_DIAGNOSTIC_WSC3_PACKAGE ? 3u : 2u,
                               world_scene_active_room_count(&sAreaScene));
            }
#endif
#if defined(IOS_CONNECTED_WORLD_TEST)
            world_region_reset((struct WorldPoint){0, 0, 0});
#endif
        }
#endif
        geo_call_global_function_nodes(&gCurrentArea->unk04->node, GEO_CONTEXT_AREA_UNLOAD);

        gCurrentArea->flags = 0;
        gCurrentArea = NULL;
        gWarpTransition.isActive = FALSE;
    }
}

void load_mario_area(void) {
    func_80320890();
    load_area(gMarioSpawnInfo->areaIndex);

    if (gCurrentArea->index == gMarioSpawnInfo->areaIndex) {
        gCurrentArea->flags |= 0x01;
        spawn_objects_from_info(0, gMarioSpawnInfo);
    }
}

void unload_mario_area(void) {
#ifdef IOS_HAT_MOVEMENT
    mario_hat_reset();
#endif
    if (gCurrentArea != NULL && (gCurrentArea->flags & 0x01)) {
        unload_objects_from_area(0, gMarioSpawnInfo->activeAreaIndex);

        gCurrentArea->flags &= ~0x01;
        if (gCurrentArea->flags == 0) {
            unload_area();
        }
    }
}

void change_area(s32 index) {
    s32 areaFlags = gCurrentArea->flags;

    if (gCurrAreaIndex != index) {
        unload_area();
        load_area(index);

        gCurrentArea->flags = areaFlags;
        gMarioObject->oActiveParticleFlags = 0;
    }

    if (areaFlags & 0x01) {
        gMarioObject->header.gfx.unk18 = index, gMarioSpawnInfo->areaIndex = index;
    }
}

void area_update_objects(void) {
    gAreaUpdateCounter++;
#if defined(IOS_CONNECTED_WORLD_TEST)
    if (gCurrentArea != NULL && gMarioObject != NULL && gMarioState != NULL) {
        world_region_update_legacy(gMarioState->pos[0], gMarioState->pos[1],
                                   gMarioState->pos[2], gMarioState->vel[0],
                                   gMarioState->vel[2], gMarioState->forwardVel, 3072);
    }
#endif
    update_objects(0);
}

/*
 * Sets up the information needed to play a warp transition, including the
 * transition type, time in frames, and the RGB color that will fill the screen.
 */
void play_transition(s16 transType, s16 time, u8 red, u8 green, u8 blue) {
    gWarpTransition.isActive = TRUE;
    gWarpTransition.type = transType;
    gWarpTransition.time = time;
    gWarpTransition.pauseRendering = FALSE;

    // The lowest bit of transType determines if the transition is fading in or out.
    if (transType & 1) {
        set_warp_transition_rgb(red, green, blue);
    } else {
        red = gWarpTransRed, green = gWarpTransGreen, blue = gWarpTransBlue;
    }

    if (transType < 8) { // if transition is RGB
        gWarpTransition.data.red = red;
        gWarpTransition.data.green = green;
        gWarpTransition.data.blue = blue;
    } else { // if transition is textured
        gWarpTransition.data.red = red;
        gWarpTransition.data.green = green;
        gWarpTransition.data.blue = blue;

        // Both the start and end textured transition are always located in the middle of the screen.
        // If you really wanted to, you could place the start at one corner and the end at
        // the opposite corner. This will make the transition image look like it is moving
        // across the screen.
        gWarpTransition.data.startTexX = SCREEN_WIDTH / 2;
        gWarpTransition.data.startTexY = SCREEN_HEIGHT / 2;
        gWarpTransition.data.endTexX = SCREEN_WIDTH / 2;
        gWarpTransition.data.endTexY = SCREEN_HEIGHT / 2;

        gWarpTransition.data.texTimer = 0;

        if (transType & 1) // Is the image fading in?
        {
            gWarpTransition.data.startTexRadius = GFX_DIMENSIONS_FULL_RADIUS;
            if (transType >= 0x0F) {
                gWarpTransition.data.endTexRadius = 16;
            } else {
                gWarpTransition.data.endTexRadius = 0;
            }
        } else // The image is fading out. (Reverses start & end circles)
        {
            if (transType >= 0x0E) {
                gWarpTransition.data.startTexRadius = 16;
            } else {
                gWarpTransition.data.startTexRadius = 0;
            }
            gWarpTransition.data.endTexRadius = GFX_DIMENSIONS_FULL_RADIUS;
        }
    }
}

/*
 * Sets up the information needed to play a warp transition, including the
 * transition type, time in frames, and the RGB color that will fill the screen.
 * The transition will play only after a number of frames specified by 'delay'
 */
void play_transition_after_delay(s16 transType, s16 time, u8 red, u8 green, u8 blue, s16 delay) {
    gWarpTransDelay = delay; // Number of frames to delay playing the transition.
    play_transition(transType, time, red, green, blue);
}

void render_game(void) {
    if (gCurrentArea != NULL && !gWarpTransition.pauseRendering) {
        geo_process_root(gCurrentArea->unk04, D_8032CE74, D_8032CE78, gFBSetColor);

        gSPViewport(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(&D_8032CF00));

        gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, BORDER_HEIGHT, SCREEN_WIDTH,
                      SCREEN_HEIGHT - BORDER_HEIGHT);
        render_hud();

        gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        render_text_labels();
#if defined(TARGET_OS_IOS) && defined(VERSION_US)
        if (sShowIntroBranding) {
            // The US HUD font has no X, Z, or period. Use the dialogue font.
            const char *name = IOS_APP_DISPLAY_NAME;
            u8 label[sizeof(IOS_APP_DISPLAY_NAME)];
            for (size_t i = 0; i < sizeof(label) - 1; ++i) {
                char c = name[i];
                label[i] = c == ' ' ? DIALOG_CHAR_SPACE
                    : c == '.' ? DIALOG_CHAR_US_PERIOD : ASCII_TO_DIALOG(c);
            }
            label[sizeof(label) - 1] = DIALOG_CHAR_TERMINATOR;
            create_dl_ortho_matrix();
            gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
            gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
            print_generic_string(get_str_x_pos_from_center(SCREEN_WIDTH / 2, label, 10.0f), 58, label);
            gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
        }
#endif
        do_cutscene_handler();
        print_displaying_credits_entry();
        gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, BORDER_HEIGHT, SCREEN_WIDTH,
                      SCREEN_HEIGHT - BORDER_HEIGHT);
        gPauseScreenMode = render_menus_and_dialogs();

        if (gPauseScreenMode != 0) {
            gSaveOptSelectIndex = gPauseScreenMode;
        }

        if (D_8032CE78 != NULL) {
            make_viewport_clip_rect(D_8032CE78);
        } else
            gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, BORDER_HEIGHT, SCREEN_WIDTH,
                          SCREEN_HEIGHT - BORDER_HEIGHT);

        if (gWarpTransition.isActive) {
            if (gWarpTransDelay == 0) {
                gWarpTransition.isActive = !render_screen_transition(0, gWarpTransition.type, gWarpTransition.time,
                                                          &gWarpTransition.data);
                if (!gWarpTransition.isActive) {
                    if (gWarpTransition.type & 1) {
                        gWarpTransition.pauseRendering = TRUE;
                    } else {
                        set_warp_transition_rgb(0, 0, 0);
                    }
                }
            } else {
                gWarpTransDelay--;
            }
        }
    } else {
        render_text_labels();
        if (D_8032CE78 != 0) {
            clear_viewport(D_8032CE78, gWarpTransFBSetColor);
        } else {
            clear_frame_buffer(gWarpTransFBSetColor);
        }
    }

    D_8032CE74 = NULL;
    D_8032CE78 = 0;
#if defined(TARGET_OS_IOS) && defined(VERSION_US)
    sShowIntroBranding = false;
#endif
}
