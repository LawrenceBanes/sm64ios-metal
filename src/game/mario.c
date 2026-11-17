#include <PR/ultratypes.h>
#include <math.h>
#include <stdio.h>

#include "sm64.h"
#include "area.h"
#include "audio/data.h"
#include "audio/external.h"
#include "behavior_actions.h"
#include "behavior_data.h"
#include "camera.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "game/portal_math.h"
#include "game/ios_reality.h"
#include "ios_kart.h"
#include "surface_terrains.h"
#include "game_init.h"
#include "interaction.h"
#include "level_table.h"
#include "level_update.h"
#include "main.h"
#include "mario.h"
#include "mario_hat.h"
#include "mario_actions_airborne.h"
#include "mario_actions_automatic.h"
#include "mario_actions_cutscene.h"
#include "mario_actions_moving.h"
#include "mario_actions_object.h"
#include "mario_actions_stationary.h"
#include "mario_actions_submerged.h"
#include "mario_misc.h"
#include "mario_step.h"
#include "memory.h"
#include "pc/controller/controller_api.h"
#include "object_fields.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "print.h"
#include "save_file.h"
#include "sound_init.h"
#include "spawn_object.h"
#include "thread6.h"
#include "pc/configfile.h"
#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD) && defined(DEBUG)
#include "game/hyrule_gate.h"
#include "level_table.h"
#include "build/hyrule_field_spawn.h"
#endif
#include "pc/cheats.h"
#ifdef IOS_BUILD
#include "pc/gfx/gfx_ios_diagnostics.h"
#define IOS_PORTAL_EVENT(...) gfx_ios_diag_log(__VA_ARGS__)
#define IOS_PORTAL_STATUS(message) gfx_ios_portal_status(message)
#define IOS_KART_EVENT(...) gfx_ios_diag_log(__VA_ARGS__)
#define IOS_KART_STATUS(message) gfx_ios_portal_status(message)
#define IOS_REALITY_EVENT(...) gfx_ios_diag_log(__VA_ARGS__)
#define IOS_REALITY_STATUS(message) gfx_ios_portal_status(message)
#else
#define IOS_PORTAL_EVENT(...) ((void)0)
#define IOS_PORTAL_STATUS(message) ((void)0)
#define IOS_KART_EVENT(...) ((void)0)
#define IOS_KART_STATUS(message) ((void)0)
#define IOS_REALITY_EVENT(...) ((void)0)
#define IOS_REALITY_STATUS(message) ((void)0)
#endif
#ifdef BETTERCAMERA
#include "bettercamera.h"
#endif

u32 unused80339F10;
s8 filler80339F1C[20];

enum {
    IOS_GAMEPLAY_REQUEST_CHARACTER = 1 << 0,
    IOS_GAMEPLAY_REQUEST_YOSHI = 1 << 1,
    IOS_GAMEPLAY_REQUEST_PORTAL_BLUE = 1 << 2,
    IOS_GAMEPLAY_REQUEST_PORTAL_ORANGE = 1 << 3,
    IOS_GAMEPLAY_REQUEST_PORTAL_ENABLE = 1 << 4,
    IOS_GAMEPLAY_REQUEST_PORTAL_RESET = 1 << 5,
    IOS_GAMEPLAY_REQUEST_KART_SPAWN = 1 << 6,
};

#define IOS_PORTAL_HALF_WIDTH 78.0f
#define IOS_PORTAL_HALF_HEIGHT 180.0f
#define IOS_PORTAL_PLAYER_RADIUS 35.0f
#define IOS_PORTAL_PLAYER_HEIGHT 160.0f
#define IOS_PORTAL_EXIT_CLEARANCE 12.0f
#define IOS_PORTAL_GROUND_SNAP_MAX 32.0f
#define IOS_PORTAL_SURFACE_NORMAL_DOT 0.995f
#define IOS_PORTAL_SURFACE_PLANE_TOLERANCE 12.0f
#define IOS_PORTAL_COVERAGE_SAMPLE_SPACING 8.0f
#define IOS_PORTAL_VISUAL_OFFSET 16.0f

static volatile u32 sIOSGameplayRequests;
static s32 sTemporaryLuigiPaletteEnabled;
static PortalFrame sIOSPortalFrames[2];
static s32 sIOSPortalValid[2];
static s32 sIOSPortalGunEnabled = TRUE;
static s32 sIOSPortalTraversalCooldown;
static struct Area *sIOSPortalArea;
static s32 sIOSPortalObjectVisibleLogged[2];
static IOSKartState sIOSKartState;
static struct Object *sIOSKartObject;
static struct Area *sIOSKartArea;
static Vec3f sIOSKartLastGroundPosition;
static s16 sIOSKartLastGroundYaw;
static s32 sIOSKartHasGroundPosition;
static u32 sIOSKartDiagnosticTicks;
static struct {
    struct Object *object;
    const BehaviorScript *behavior;
    f32 homeX, homeY, homeZ;
} sIOSRealitySpawnedObjects[8];
static struct Area *sIOSRealitySpawnArea;
static s32 sIOSRealityLoggedResultId = -1;
static s32 sIOSRealityLoggedResult = -1;

#define IOS_KART_MOUNT_DISTANCE 130.0f
#define IOS_KART_PI 3.14159265358979323846f
#define IOS_KART_DRIVER_RENDER_HEIGHT 7.0f
#define IOS_KART_DIAGNOSTIC_INTERVAL 60
#define IOS_KART_WHEEL_RADIUS 18.0f

static const char *mario_ios_portal_name(s32 portalType) {
    return portalType == MARIO_IOS_PORTAL_BLUE ? "blue" : "orange";
}

static void mario_ios_portal_feedback(s32 portalType, const char *result) {
    char message[128];
    snprintf(message, sizeof(message), "%s portal %s", portalType == MARIO_IOS_PORTAL_BLUE ? "Blue" : "Orange", result);
    IOS_PORTAL_STATUS(message);
}

static void mario_ios_clear_portals(void) {
    sIOSPortalValid[MARIO_IOS_PORTAL_BLUE] = FALSE;
    sIOSPortalValid[MARIO_IOS_PORTAL_ORANGE] = FALSE;
    sIOSPortalTraversalCooldown = 0;
    sIOSPortalObjectVisibleLogged[MARIO_IOS_PORTAL_BLUE] = FALSE;
    sIOSPortalObjectVisibleLogged[MARIO_IOS_PORTAL_ORANGE] = FALSE;
}

static void mario_ios_reset_portals(void) {
    mario_ios_clear_portals();
    sIOSPortalArea = gCurrentArea;
}

static void mario_ios_validate_portal_area(void) {
    if (sIOSPortalArea != gCurrentArea) {
        IOS_PORTAL_EVENT("Portal state cleared on area transition oldArea=%d newArea=%d", sIOSPortalArea ? sIOSPortalArea->index : -1, gCurrentArea ? gCurrentArea->index : -1);
        mario_ios_clear_portals();
        sIOSPortalArea = gCurrentArea;
    }
}

void mario_ios_request_character_switch(void) {
    __atomic_fetch_or(&sIOSGameplayRequests, IOS_GAMEPLAY_REQUEST_CHARACTER, __ATOMIC_RELAXED);
}

void mario_ios_request_yoshi_toggle(void) {
    __atomic_fetch_or(&sIOSGameplayRequests, IOS_GAMEPLAY_REQUEST_YOSHI, __ATOMIC_RELAXED);
}

void mario_ios_request_portal_blue(void) {
    __atomic_fetch_or(&sIOSGameplayRequests, IOS_GAMEPLAY_REQUEST_PORTAL_BLUE, __ATOMIC_RELAXED);
}

void mario_ios_request_portal_orange(void) {
    __atomic_fetch_or(&sIOSGameplayRequests, IOS_GAMEPLAY_REQUEST_PORTAL_ORANGE, __ATOMIC_RELAXED);
}

void mario_ios_request_portal_enable_toggle(void) {
    __atomic_fetch_or(&sIOSGameplayRequests, IOS_GAMEPLAY_REQUEST_PORTAL_ENABLE, __ATOMIC_RELAXED);
}

void mario_ios_request_portal_reset(void) {
    __atomic_fetch_or(&sIOSGameplayRequests, IOS_GAMEPLAY_REQUEST_PORTAL_RESET, __ATOMIC_RELAXED);
}

s32 mario_ios_is_riding_yoshi(const struct MarioState *m) {
    return m != NULL && m->riddenObj != NULL
        && m->riddenObj->behavior == segmented_to_virtual(bhvRideableYoshi)
        && (m->action == ACT_RIDING_YOSHI_GROUND || m->action == ACT_RIDING_YOSHI_AIR);
}

void mario_ios_detach_yoshi(struct MarioState *m) {
    if (m != NULL && m->riddenObj != NULL
        && m->riddenObj->behavior == segmented_to_virtual(bhvRideableYoshi)) {
        m->riddenObj->oAction = 0;
        m->riddenObj = NULL;
    }
}

static struct Object *mario_ios_nearby_yoshi(f32 maxDistance) {
    struct Object *yoshi;
    f32 distance;

    yoshi = cur_obj_find_nearest_object_with_behavior(bhvRideableYoshi, &distance);
    return (yoshi != NULL && distance <= maxDistance) ? yoshi : NULL;
}

static s32 mario_ios_try_mount_yoshi(struct MarioState *m) {
    struct Object *yoshi;
    u32 actionGroup = m->action & ACT_GROUP_MASK;

    if (m->riddenObj != NULL || (actionGroup != ACT_GROUP_STATIONARY && actionGroup != ACT_GROUP_MOVING)
        || (m->action & (ACT_FLAG_AIR | ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER | ACT_FLAG_INTANGIBLE))) {
        return FALSE;
    }

    yoshi = mario_ios_nearby_yoshi(240.0f);
    if (yoshi == NULL) {
        return FALSE;
    }

    m->riddenObj = yoshi;
    yoshi->oAction = 1;
    mario_set_forward_vel(m, 0.0f);
    return set_mario_action(m, ACT_RIDING_YOSHI_GROUND, 1);
}

static void mario_ios_validate_kart_area(struct MarioState *m) {
    if (sIOSKartArea != gCurrentArea) {
        if (m != NULL && m->riddenObj == sIOSKartObject) m->riddenObj = NULL;
        sIOSKartObject = NULL;
        sIOSKartArea = gCurrentArea;
        ios_kart_reset(&sIOSKartState, 0.0f);
        sIOSKartHasGroundPosition = FALSE;
        IOS_KART_EVENT("Kart state cleared on area transition area=%d", gCurrentArea ? gCurrentArea->index : -1);
    }
}

static struct Object *mario_ios_find_area_kart(void) {
    f32 distance;
    if (sIOSKartArea != gCurrentArea) mario_ios_validate_kart_area(gMarioState);
    if (sIOSKartObject != NULL) return sIOSKartObject;
    sIOSKartObject = cur_obj_find_nearest_object_with_behavior(bhvIOSKart, &distance);
    if (sIOSKartObject != NULL) sIOSKartArea = gCurrentArea;
    return sIOSKartObject;
}

s32 mario_ios_is_driving_kart(const struct MarioState *m) {
    return m != NULL && sIOSKartArea == gCurrentArea && sIOSKartObject != NULL
        && m->riddenObj == sIOSKartObject
        && (m->action == ACT_IOS_KART_GROUND || m->action == ACT_IOS_KART_AIR);
}

void mario_ios_register_kart(struct Object *kart) {
    if (kart == NULL) return;
    if (sIOSKartArea != gCurrentArea || sIOSKartObject == NULL) {
        sIOSKartObject = kart;
        sIOSKartArea = gCurrentArea;
        ios_kart_reset(&sIOSKartState, (f32)kart->oMoveAngleYaw * (IOS_KART_PI / 32768.0f));
        sIOSKartLastGroundPosition[0] = kart->oPosX;
        sIOSKartLastGroundPosition[1] = kart->oPosY;
        sIOSKartLastGroundPosition[2] = kart->oPosZ;
        sIOSKartLastGroundYaw = kart->oMoveAngleYaw;
        sIOSKartHasGroundPosition = TRUE;
        IOS_KART_EVENT("Kart actor registered object=%p area=%d position=(%.1f,%.1f,%.1f)", kart,
                       gCurrentArea ? gCurrentArea->index : -1, kart->oPosX, kart->oPosY, kart->oPosZ);
    }
}

static s32 mario_ios_get_kart_spawn_position(struct MarioState *m, Vec3f position) {
    struct Surface *floor = NULL;
    struct Surface *ceil = NULL;
    f32 yaw = (f32)m->faceAngle[1] * (IOS_KART_PI / 32768.0f);
    f32 x = m->pos[0] + sinf(yaw) * 180.0f;
    f32 z = m->pos[2] + cosf(yaw) * 180.0f;
    f32 y = find_floor(x, m->pos[1] + 400.0f, z, &floor);
    if (floor == NULL || floor->normal.y < 0.5f || y < m->pos[1] - 240.0f || y > m->pos[1] + 180.0f) {
        IOS_KART_EVENT("Kart placement rejected reason=%s floorHeight=%.1f playerY=%.1f",
                       floor == NULL ? "no floor" : "unsafe floor or height", y, m->pos[1]);
        IOS_KART_STATUS("No safe ground nearby for a kart");
        return FALSE;
    }
    y += 1.0f;
    f32_find_wall_collision(&x, &y, &z, 62.0f, 24.0f);
    f32_find_wall_collision(&x, &y, &z, 74.0f, 180.0f);
    y = find_floor(x, y + 220.0f, z, &floor);
    position[0] = x;
    position[1] = y;
    position[2] = z;
    if (floor == NULL || floor->normal.y < 0.5f
        || y < m->pos[1] - 240.0f || y > m->pos[1] + 180.0f
        || sqrtf((x - m->pos[0]) * (x - m->pos[0]) + (z - m->pos[2]) * (z - m->pos[2])) < 125.0f
        || vec3f_find_ceil(position, y, &ceil) - y < 220.0f) {
        IOS_KART_EVENT("Kart placement rejected after collision clearance position=(%.1f,%.1f,%.1f)", x, y, z);
        IOS_KART_STATUS("Nearby space is too tight for the kart");
        return FALSE;
    }
    return TRUE;
}

static void mario_ios_position_kart(struct MarioState *m, struct Object *kart, const Vec3f position) {
    f32 yaw = (f32)m->faceAngle[1] * (IOS_KART_PI / 32768.0f);
    kart->oPosX = position[0];
    kart->oPosY = position[1];
    kart->oPosZ = position[2];
    kart->oHomeX = kart->oPosX;
    kart->oHomeY = kart->oPosY;
    kart->oHomeZ = kart->oPosZ;
    kart->oMoveAngleYaw = m->faceAngle[1];
    kart->oAction = 0;
    kart->oForwardVel = 0.0f;
    ios_kart_reset(&sIOSKartState, yaw);
    sIOSKartLastGroundPosition[0] = kart->oPosX;
    sIOSKartLastGroundPosition[1] = kart->oPosY;
    sIOSKartLastGroundPosition[2] = kart->oPosZ;
    sIOSKartLastGroundYaw = kart->oMoveAngleYaw;
    sIOSKartHasGroundPosition = TRUE;
}

void mario_ios_request_kart_spawn(void) {
    __atomic_fetch_or(&sIOSGameplayRequests, IOS_GAMEPLAY_REQUEST_KART_SPAWN, __ATOMIC_RELAXED);
}

static struct Object *mario_ios_spawn_or_reposition_kart(struct MarioState *m) {
    Vec3f position;
    if (!mario_ios_get_kart_spawn_position(m, position)) return NULL;

    struct Object *kart = mario_ios_find_area_kart();
    if (kart == NULL) {
        kart = spawn_object(m->marioObj, MODEL_IOS_KART, bhvIOSKart);
        if (kart == NULL) {
            IOS_KART_EVENT("Kart spawn failed: object pool returned null");
            IOS_KART_STATUS("Kart could not spawn");
            return NULL;
        }
        sIOSKartObject = kart;
        sIOSKartArea = gCurrentArea;
    }
    mario_ios_position_kart(m, kart, position);
    IOS_KART_EVENT("Kart spawned/repositioned object=%p area=%d position=(%.1f,%.1f,%.1f); use L to mount", kart,
                   gCurrentArea ? gCurrentArea->index : -1, kart->oPosX, kart->oPosY, kart->oPosZ);
    IOS_KART_STATUS("Kart ready nearby. Press L to drive");
    return kart;
}

static s32 mario_ios_get_kart_mount_position(struct Object *kart, Vec3f position,
                                            struct Surface **mountFloor) {
    struct Surface *floor = NULL;
    struct Surface *ceil = NULL;
    f32 floorHeight;
    f32 originalX = kart->oPosX;
    f32 originalZ = kart->oPosZ;
    position[0] = kart->oPosX;
    position[1] = kart->oPosY;
    position[2] = kart->oPosZ;
    floorHeight = find_floor(position[0], position[1] + 220.0f, position[2], &floor);
    if (floor == NULL || floor->normal.y < 0.5f
        || fabsf(floorHeight - kart->oPosY) > 100.0f) return FALSE;

    position[1] = floorHeight + 1.0f;
    f32_find_wall_collision(&position[0], &position[1], &position[2], 62.0f, 24.0f);
    f32_find_wall_collision(&position[0], &position[1], &position[2], 74.0f, 180.0f);
    floorHeight = find_floor(position[0], position[1] + 220.0f, position[2], &floor);
    if (floor == NULL || floor->normal.y < 0.5f
        || fabsf(floorHeight - kart->oPosY) > 100.0f
        || sqrtf((position[0] - originalX) * (position[0] - originalX)
               + (position[2] - originalZ) * (position[2] - originalZ)) > 100.0f) return FALSE;

    position[1] = floorHeight;
    if (vec3f_find_ceil(position, floorHeight, &ceil) - floorHeight < 220.0f) return FALSE;
    *mountFloor = floor;
    return TRUE;
}

static s32 mario_ios_try_mount_kart(struct MarioState *m) {
    struct Object *kart;
    Vec3f mountPosition;
    struct Surface *mountFloor = NULL;
    f32 distance;
    u32 group = m->action & ACT_GROUP_MASK;
    s32 grounded = (group == ACT_GROUP_STATIONARY || group == ACT_GROUP_MOVING)
        && !(m->action & (ACT_FLAG_AIR | ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER | ACT_FLAG_INTANGIBLE))
        && m->floor != NULL;
    if (!grounded || m->riddenObj != NULL) {
        IOS_KART_STATUS("Land and dismount your current ride first");
        return FALSE;
    }
    kart = mario_ios_find_area_kart();
    if (kart == NULL) {
        IOS_KART_STATUS("No kart here. Use Gameplay menu to spawn one");
        return FALSE;
    }
    distance = sqrtf((m->pos[0] - kart->oPosX) * (m->pos[0] - kart->oPosX)
                   + (m->pos[1] - kart->oPosY) * (m->pos[1] - kart->oPosY)
                   + (m->pos[2] - kart->oPosZ) * (m->pos[2] - kart->oPosZ));
    if (!ios_kart_can_mount(distance, IOS_KART_MOUNT_DISTANCE, grounded,
                            m->riddenObj != NULL, kart->oAction != 0)) {
        if (distance > IOS_KART_MOUNT_DISTANCE) {
            IOS_KART_EVENT("Kart mount rejected reason=too far distance=%.1f", distance);
            IOS_KART_STATUS("Walk closer to the kart and press L");
        } else {
            IOS_KART_EVENT("Kart mount rejected reason=occupied object=%p", kart);
            IOS_KART_STATUS("That kart is already occupied");
        }
        return FALSE;
    }
    if (!mario_ios_get_kart_mount_position(kart, mountPosition, &mountFloor)) {
        IOS_KART_EVENT("Kart mount rejected reason=no safe driver position object=%p", kart);
        IOS_KART_STATUS("Kart is blocked in; reposition it from Gameplay");
        return FALSE;
    }
    sIOSKartObject = kart;
    sIOSKartArea = gCurrentArea;
    m->riddenObj = kart;
    kart->oAction = 1;
    kart->oPosX = mountPosition[0];
    kart->oPosY = mountPosition[1];
    kart->oPosZ = mountPosition[2];
    m->pos[0] = mountPosition[0];
    m->pos[1] = mountPosition[1];
    m->pos[2] = mountPosition[2];
    m->floor = mountFloor;
    m->floorHeight = mountPosition[1];
    m->floorAngle = atan2s(mountFloor->normal.z, mountFloor->normal.x);
    m->faceAngle[1] = kart->oMoveAngleYaw;
    ios_kart_reset(&sIOSKartState, (f32)m->faceAngle[1] * (IOS_KART_PI / 32768.0f));
    vec3f_copy(sIOSKartLastGroundPosition, m->pos);
    sIOSKartLastGroundYaw = m->faceAngle[1];
    sIOSKartHasGroundPosition = TRUE;
    m->vel[0] = m->vel[1] = m->vel[2] = 0.0f;
    m->slideVelX = m->slideVelZ = 0.0f;
    mario_set_forward_vel(m, 0.0f);
    IOS_KART_EVENT("Kart mounted object=%p distance=%.1f driver=%s position=(%.1f,%.1f,%.1f)", kart, distance,
                   sTemporaryLuigiPaletteEnabled ? "Luigi" : "Mario", m->pos[0], m->pos[1], m->pos[2]);
    IOS_KART_STATUS("Driving: left/right steers; up or B accelerates; down reverses; Z brakes; A hops/drifts");
    return set_mario_action(m, ACT_IOS_KART_GROUND, 1);
}

static void mario_ios_clear_kart_mount(struct MarioState *m) {
    struct Object *kart;
    if (m == NULL || sIOSKartArea != gCurrentArea || m->riddenObj != sIOSKartObject) return;
    kart = sIOSKartObject;
    kart->oAction = 0;
    m->riddenObj = NULL;
    sIOSKartState.forwardSpeed = 0.0f;
    sIOSKartState.lateralSpeed = 0.0f;
    sIOSKartState.yawRateRadians = 0.0f;
    sIOSKartState.drifting = FALSE;
    sIOSKartState.boostReady = FALSE;
    sIOSKartState.driftChargeFrames = 0;
    sIOSKartState.boostFrames = 0;
}

static void mario_ios_restore_kart_to_last_ground(void) {
    if (sIOSKartObject == NULL || !sIOSKartHasGroundPosition) return;
    sIOSKartObject->oPosX = sIOSKartLastGroundPosition[0];
    sIOSKartObject->oPosY = sIOSKartLastGroundPosition[1];
    sIOSKartObject->oPosZ = sIOSKartLastGroundPosition[2];
    sIOSKartObject->oMoveAngleYaw = sIOSKartLastGroundYaw;
    sIOSKartObject->oAction = 0;
}

static void mario_ios_detach_interrupted_kart(struct MarioState *m) {
    if (m == NULL || sIOSKartObject == NULL || sIOSKartArea != gCurrentArea
        || m->riddenObj != sIOSKartObject || mario_ios_is_driving_kart(m)) return;
    if (m->floor == NULL || (m->action & ACT_GROUP_MASK) == ACT_GROUP_AIRBORNE) {
        mario_ios_restore_kart_to_last_ground();
    } else {
        sIOSKartObject->oPosX = m->pos[0];
        sIOSKartObject->oPosY = m->pos[1];
        sIOSKartObject->oPosZ = m->pos[2];
        sIOSKartObject->oMoveAngleYaw = m->faceAngle[1];
        sIOSKartObject->oAction = 0;
    }
    mario_ios_clear_kart_mount(m);
    IOS_KART_EVENT("Kart mount released after interrupted action action=0x%08x", m->action);
}

static s32 mario_ios_find_driver_exit_position(struct MarioState *m, Vec3f exitPosition,
                                              struct Surface **exitFloor) {
    s32 side;
    for (side = 1; side >= -1; side -= 2) {
        struct Surface *floor = NULL;
        struct Surface *ceil = NULL;
        Vec3f pos;
        f32 floorHeight;
        pos[0] = m->pos[0] + coss(m->faceAngle[1]) * 92.0f * side;
        pos[1] = m->pos[1];
        pos[2] = m->pos[2] - sins(m->faceAngle[1]) * 92.0f * side;
        floorHeight = find_floor(pos[0], pos[1] + 120.0f, pos[2], &floor);
        if (floor == NULL || floor->normal.y < 0.5f
            || floorHeight < m->pos[1] - 100.0f || floorHeight > m->pos[1] + 80.0f) continue;
        pos[1] = floorHeight;
        if (vec3f_find_ceil(pos, floorHeight, &ceil) - floorHeight < 160.0f) continue;
        f32_find_wall_collision(&pos[0], &pos[1], &pos[2], 35.0f, 24.0f);
        floorHeight = find_floor(pos[0], pos[1] + 80.0f, pos[2], &floor);
        if (floor == NULL || floor->normal.y < 0.5f
            || floorHeight < m->pos[1] - 100.0f || floorHeight > m->pos[1] + 80.0f) continue;
        pos[1] = floorHeight;
        if (vec3f_find_ceil(pos, floorHeight, &ceil) - floorHeight < 160.0f) continue;
        vec3f_copy(exitPosition, pos);
        *exitFloor = floor;
        return TRUE;
    }
    return FALSE;
}

static void mario_ios_dismount_kart(struct MarioState *m, s32 keepCurrentAction) {
    Vec3f exitPosition;
    struct Surface *exitFloor = NULL;
    if (!mario_ios_is_driving_kart(m)) return;
    if (!ios_kart_can_dismount(m->action != ACT_IOS_KART_AIR, TRUE)) {
        IOS_KART_STATUS("Land before exiting the kart");
        return;
    }
    if (!mario_ios_find_driver_exit_position(m, exitPosition, &exitFloor)) {
        IOS_KART_STATUS("No safe space beside the kart to exit");
        return;
    }
    if (!ios_kart_can_dismount(TRUE, exitFloor != NULL)) return;
    sIOSKartObject->oPosX = m->pos[0];
    sIOSKartObject->oPosY = m->pos[1];
    sIOSKartObject->oPosZ = m->pos[2];
    sIOSKartObject->oMoveAngleYaw = m->faceAngle[1];
    mario_ios_kart_record_ground_position(m, TRUE);
    mario_ios_clear_kart_mount(m);
    vec3f_copy(m->pos, exitPosition);
    m->floor = exitFloor;
    m->floorHeight = m->pos[1];
    m->floorAngle = atan2s(exitFloor->normal.z, exitFloor->normal.x);
    mario_set_forward_vel(m, 0.0f);
    m->marioObj->header.gfx.pos[0] = m->pos[0];
    m->marioObj->header.gfx.pos[1] = m->pos[1];
    m->marioObj->header.gfx.pos[2] = m->pos[2];
    m->faceAngle[0] = 0;
    m->faceAngle[2] = 0;
    if (!keepCurrentAction) set_mario_action(m, ACT_WALKING, 0);
    IOS_KART_EVENT("Kart dismounted position=(%.1f,%.1f,%.1f)", m->pos[0], m->pos[1], m->pos[2]);
    IOS_KART_STATUS("Kart dismounted");
}

static void mario_ios_process_kart_interaction(struct MarioState *m) {
    if (m->controller == NULL || !(m->controller->buttonPressed & L_TRIG)) return;
    if (mario_ios_is_driving_kart(m)) {
        mario_ios_dismount_kart(m, FALSE);
    } else {
        mario_ios_try_mount_kart(m);
    }
}

void mario_ios_update_kart(struct MarioState *m, s32 grounded, IOSKartEvents *events) {
    IOSKartInput input = {0};
    f32 stickX = 0.0f, stickY = 0.0f;
    f32 velocityX, velocityZ;
    s32 yaw;
    if (m->controller != NULL) {
        stickX = m->controller->stickX / 64.0f;
        stickY = m->controller->stickY / 64.0f;
        ios_kart_map_controller_input(&input, stickX, stickY,
                                     (m->controller->buttonDown & B_BUTTON) != 0,
                                     (m->controller->buttonDown & Z_TRIG) != 0,
                                     (m->controller->buttonDown & A_BUTTON) != 0,
                                     (m->controller->buttonPressed & A_BUTTON) != 0);
    }
    input.grounded = grounded;
    input.traction = mario_floor_is_slippery(m) ? 0.68f : 1.0f;
    if (grounded && m->floor != NULL) {
        input.slopeAcceleration = ios_kart_slope_acceleration(m->floor->normal.x,
                                                              m->floor->normal.y,
                                                              m->floor->normal.z,
                                                              sIOSKartState.headingRadians);
    }

    ios_kart_update(&sIOSKartState, &input, events);
    yaw = (s32)(sIOSKartState.headingRadians * (32768.0f / IOS_KART_PI));
    m->faceAngle[1] = (s16)yaw;
    m->forwardVel = sIOSKartState.forwardSpeed;
    ios_kart_velocity(&sIOSKartState, &velocityX, &velocityZ);
    m->vel[0] = m->slideVelX = velocityX;
    m->vel[2] = m->slideVelZ = velocityZ;
    if (grounded) m->vel[1] = 0.0f;

    if (m->controller != NULL && (++sIOSKartDiagnosticTicks % IOS_KART_DIAGNOSTIC_INTERVAL) == 0) {
        f32 forwardX = sinf(sIOSKartState.headingRadians);
        f32 forwardZ = cosf(sIOSKartState.headingRadians);
        IOS_KART_EVENT("Kart controls tick=%u source=%d rawStick=(%d,%d) shapedStick=(%.1f,%.1f) mapped=(steer=%.2f throttle=%.2f brake=%.2f reverse=%.2f) buttons=(B=%d Z=%d A=%d) nose=(%.2f,%.2f) velocity=(%.2f,%.2f) speed=%.2f yaw=%.3f yawRate=%.3f steerAngle=%.3f drift=%d driftDir=%d grounded=%d cameraYaw=%d",
                       sIOSKartDiagnosticTicks, get_current_input(), m->controller->rawStickX,
                       m->controller->rawStickY, m->controller->stickX, m->controller->stickY,
                       input.steer, input.throttle, input.brake, input.reverse,
                       (m->controller->buttonDown & B_BUTTON) != 0,
                       (m->controller->buttonDown & Z_TRIG) != 0,
                       (m->controller->buttonDown & A_BUTTON) != 0,
                       forwardX, forwardZ, velocityX, velocityZ, sIOSKartState.forwardSpeed,
                       sIOSKartState.headingRadians, sIOSKartState.yawRateRadians,
                       sIOSKartState.steeringAngleRadians, sIOSKartState.drifting,
                       sIOSKartState.driftDirection, grounded,
                       (m->area != NULL && m->area->camera != NULL) ? m->area->camera->yaw : 0);
    }

    if (events->driftStarted) {
        IOS_KART_EVENT("Kart powerslide started speed=%.1f steer=%.2f", sIOSKartState.forwardSpeed, input.steer);
        IOS_KART_STATUS("Powerslide");
    }
    if (events->boostReady) {
        IOS_KART_EVENT("Kart mini-turbo ready charge=%d", sIOSKartState.driftChargeFrames);
        IOS_KART_STATUS("Mini-turbo ready. Release A to boost");
    }
    if (events->boostActivated) {
        IOS_KART_EVENT("Kart mini-turbo activated speed=%.1f duration=%d", sIOSKartState.forwardSpeed,
                       sIOSKartState.boostFrames);
        IOS_KART_STATUS("Mini-turbo!");
    }
}

void mario_ios_kart_record_ground_position(struct MarioState *m, s32 grounded) {
    if (!grounded || m == NULL || sIOSKartObject == NULL || sIOSKartArea != gCurrentArea) return;
    vec3f_copy(sIOSKartLastGroundPosition, m->pos);
    sIOSKartLastGroundYaw = m->faceAngle[1];
    sIOSKartHasGroundPosition = TRUE;
}

void mario_ios_kart_wall_collision(struct MarioState *m) {
    f32 velocityX, velocityZ;
    if (m->wall == NULL) return;
    ios_kart_wall_collision(&sIOSKartState, m->wall->normal.x, m->wall->normal.z);
    m->forwardVel = sIOSKartState.forwardSpeed;
    ios_kart_velocity(&sIOSKartState, &velocityX, &velocityZ);
    m->vel[0] = m->slideVelX = velocityX;
    m->vel[2] = m->slideVelZ = velocityZ;
    IOS_KART_EVENT("Kart wall response normal=(%.2f,%.2f) speed=%.1f", m->wall->normal.x,
                   m->wall->normal.z, sIOSKartState.forwardSpeed);
}

s32 mario_ios_kart_boost_active(void) { return sIOSKartState.boostFrames > 0; }
s32 mario_ios_kart_boost_ready(void) { return sIOSKartState.boostReady; }

void mario_ios_sync_kart_object(struct Object *kart) {
    struct MarioState *m = gMarioState;
    f32 pulse = 1.0f;
    if (kart == NULL) return;
    if (kart == sIOSKartObject && sIOSKartArea == gCurrentArea && mario_ios_is_driving_kart(m)) {
        kart->oPosX = m->pos[0];
        kart->oPosY = m->pos[1];
        kart->oPosZ = m->pos[2];
        kart->oMoveAngleYaw = m->faceAngle[1];
        kart->oForwardVel = sIOSKartState.forwardSpeed;
        kart->oAngleVelYaw = (s32)(sIOSKartState.steeringAngleRadians * (32768.0f / IOS_KART_PI));
        kart->oAnimState = (s16)(kart->oAnimState
            + (s32)(sIOSKartState.forwardSpeed * (32768.0f / (IOS_KART_PI * IOS_KART_WHEEL_RADIUS))));
        if (m->floor != NULL && m->floor->normal.y > 0.1f) {
            f32 sinHeading = sinf(sIOSKartState.headingRadians);
            f32 cosHeading = cosf(sIOSKartState.headingRadians);
            f32 forwardSlope = m->floor->normal.x * sinHeading + m->floor->normal.z * cosHeading;
            f32 sideSlope = m->floor->normal.x * cosHeading - m->floor->normal.z * sinHeading;
            kart->oMoveAnglePitch = atan2s(-forwardSlope, m->floor->normal.y);
            kart->oMoveAngleRoll = atan2s(-sideSlope, m->floor->normal.y);
        } else {
            kart->oMoveAnglePitch = 0;
            kart->oMoveAngleRoll = 0;
        }
        if (sIOSKartState.drifting) {
            kart->oMoveAngleRoll -= (s16)(sIOSKartState.yawRateRadians * 4500.0f);
        }
        if (sIOSKartState.boostFrames > 0) pulse += 0.04f + 0.02f * sins((s16)(gGlobalTimer * 0x1800));
        obj_scale(kart, pulse);
    } else if (kart->oAction == 1) {
        kart->oAction = 0;
        kart->oForwardVel = 0.0f;
        kart->oAngleVelYaw = 0;
        obj_scale(kart, 1.0f);
    } else {
        kart->oForwardVel = 0.0f;
        kart->oAngleVelYaw = 0;
    }
    obj_set_gfx_pos_from_pos(kart);
    obj_set_angle(kart, kart->oMoveAnglePitch, kart->oMoveAngleYaw, kart->oMoveAngleRoll);
    kart->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
}

static void mario_ios_portal_triangle_from_surface(const struct Surface *surface,
                                                   PortalTriangle *triangle) {
    s32 vertex;
    const s16 *source[3] = {surface->vertex1, surface->vertex2, surface->vertex3};
    for (vertex = 0; vertex < 3; ++vertex) {
        triangle->vertex[vertex][0] = source[vertex][0];
        triangle->vertex[vertex][1] = source[vertex][1];
        triangle->vertex[vertex][2] = source[vertex][2];
    }
    triangle->normal[0] = surface->normal.x;
    triangle->normal[1] = surface->normal.y;
    triangle->normal[2] = surface->normal.z;
}

static s32 mario_ios_surface_matches_portal(const struct Surface *surface,
                                             const PortalFrame *frame) {
    PortalTriangle triangle;
    if (surface == NULL || (surface->flags & SURFACE_FLAG_DYNAMIC)
        || surface->type == SURFACE_CAMERA_BOUNDARY) return FALSE;
    mario_ios_portal_triangle_from_surface(surface, &triangle);
    return portal_triangle_matches_frame(frame, &triangle, IOS_PORTAL_SURFACE_NORMAL_DOT,
                                         IOS_PORTAL_SURFACE_PLANE_TOLERANCE);
}

static s32 mario_ios_portal_point_has_static_support(const PortalFrame *frame,
                                                     f32 localRight, f32 localUp) {
    Vec3f point;
    s32 cellX, cellZ, dx, dz, partition;
    point[0] = frame->position[0] + localRight * frame->right[0] + localUp * frame->up[0];
    point[1] = frame->position[1] + localRight * frame->right[1] + localUp * frame->up[1];
    point[2] = frame->position[2] + localRight * frame->right[2] + localUp * frame->up[2];
    cellX = (s32)((point[0] + LEVEL_BOUNDARY_MAX) / CELL_SIZE);
    cellZ = (s32)((point[2] + LEVEL_BOUNDARY_MAX) / CELL_SIZE);

    /* A wall triangle can be registered in adjacent X/Z cells, especially
     * where its plane is axis-aligned. Search the point cell and its neighbors
     * in every collision class, but accept only static coplanar host pieces. */
    for (dz = -1; dz <= 1; ++dz) for (dx = -1; dx <= 1; ++dx) {
        s32 cx = cellX + dx, cz = cellZ + dz;
        if (cx < 0 || cx >= SURFACE_PARTITION_DIM
            || cz < 0 || cz >= SURFACE_PARTITION_DIM) continue;
        for (partition = SPATIAL_PARTITION_FLOORS; partition <= SPATIAL_PARTITION_WALLS; ++partition) {
            struct SurfaceNode *node = gStaticSurfacePartition[cz][cx][partition].next;
            for (; node != NULL; node = node->next) {
                struct Surface *surface = node->surface;
                float vertices[3][3];
                if (!mario_ios_surface_matches_portal(surface, frame)) continue;
                vertices[0][0] = surface->vertex1[0]; vertices[0][1] = surface->vertex1[1]; vertices[0][2] = surface->vertex1[2];
                vertices[1][0] = surface->vertex2[0]; vertices[1][1] = surface->vertex2[1]; vertices[1][2] = surface->vertex2[2];
                vertices[2][0] = surface->vertex3[0]; vertices[2][1] = surface->vertex3[1]; vertices[2][2] = surface->vertex3[2];
                if (portal_point_in_triangle_frame(frame, localRight, localUp, vertices)) return TRUE;
            }
        }
    }
    return FALSE;
}

static s32 mario_ios_portal_aperture_has_support(const PortalFrame *frame) {
    s32 columns = (s32)ceilf((2.0f * IOS_PORTAL_HALF_WIDTH) / IOS_PORTAL_COVERAGE_SAMPLE_SPACING);
    s32 rows = (s32)ceilf((2.0f * IOS_PORTAL_HALF_HEIGHT) / IOS_PORTAL_COVERAGE_SAMPLE_SPACING);
    s32 x, y;
    for (y = 0; y <= rows; ++y) {
        f32 localUp = -IOS_PORTAL_HALF_HEIGHT + (2.0f * IOS_PORTAL_HALF_HEIGHT * y) / rows;
        for (x = 0; x <= columns; ++x) {
            f32 localRight = -IOS_PORTAL_HALF_WIDTH + (2.0f * IOS_PORTAL_HALF_WIDTH * x) / columns;
            if (!mario_ios_portal_point_has_static_support(frame, localRight, localUp)) return FALSE;
        }
    }
    return TRUE;
}

static s32 mario_ios_point_in_aperture_rect(f32 x, f32 y, f32 halfWidth, f32 halfHeight) {
    return x >= -halfWidth && x <= halfWidth && y >= -halfHeight && y <= halfHeight;
}

static s32 mario_ios_segments_intersect_2d(const float a[2], const float b[2],
                                          const float c[2], const float d[2]) {
    float ab_c = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    float ab_d = (b[0] - a[0]) * (d[1] - a[1]) - (b[1] - a[1]) * (d[0] - a[0]);
    float cd_a = (d[0] - c[0]) * (a[1] - c[1]) - (d[1] - c[1]) * (a[0] - c[0]);
    float cd_b = (d[0] - c[0]) * (b[1] - c[1]) - (d[1] - c[1]) * (b[0] - c[0]);
    const float epsilon = 0.0001f;
    if (((ab_c > epsilon && ab_d > epsilon) || (ab_c < -epsilon && ab_d < -epsilon))
        || ((cd_a > epsilon && cd_b > epsilon) || (cd_a < -epsilon && cd_b < -epsilon))) return FALSE;
#define PORTAL_ON_SEGMENT(p, q, r) \
    ((q)[0] >= fminf((p)[0], (r)[0]) - epsilon && (q)[0] <= fmaxf((p)[0], (r)[0]) + epsilon \
     && (q)[1] >= fminf((p)[1], (r)[1]) - epsilon && (q)[1] <= fmaxf((p)[1], (r)[1]) + epsilon)
    if (fabsf(ab_c) <= epsilon && PORTAL_ON_SEGMENT(a, c, b)) return TRUE;
    if (fabsf(ab_d) <= epsilon && PORTAL_ON_SEGMENT(a, d, b)) return TRUE;
    if (fabsf(cd_a) <= epsilon && PORTAL_ON_SEGMENT(c, a, d)) return TRUE;
    if (fabsf(cd_b) <= epsilon && PORTAL_ON_SEGMENT(c, b, d)) return TRUE;
#undef PORTAL_ON_SEGMENT
    return ((ab_c < 0.0f && ab_d > 0.0f) || (ab_c > 0.0f && ab_d < 0.0f))
        && ((cd_a < 0.0f && cd_b > 0.0f) || (cd_a > 0.0f && cd_b < 0.0f));
}

static s32 mario_ios_triangle_overlaps_aperture(const PortalFrame *frame,
                                               const PortalTriangle *triangle) {
    float projected[3][2];
    float corners[4][2] = {
        {-IOS_PORTAL_HALF_WIDTH, -IOS_PORTAL_HALF_HEIGHT},
        { IOS_PORTAL_HALF_WIDTH, -IOS_PORTAL_HALF_HEIGHT},
        { IOS_PORTAL_HALF_WIDTH,  IOS_PORTAL_HALF_HEIGHT},
        {-IOS_PORTAL_HALF_WIDTH,  IOS_PORTAL_HALF_HEIGHT},
    };
    s32 i, j;
    for (i = 0; i < 3; ++i) {
        portal_project_to_frame(frame, triangle->vertex[i], &projected[i][0], &projected[i][1], NULL);
        if (mario_ios_point_in_aperture_rect(projected[i][0], projected[i][1],
                                             IOS_PORTAL_HALF_WIDTH, IOS_PORTAL_HALF_HEIGHT)) return TRUE;
    }
    for (i = 0; i < 4; ++i)
        if (portal_point_in_triangle_2d(corners[i][0], corners[i][1],
                                       projected[0], projected[1], projected[2])) return TRUE;
    for (i = 0; i < 3; ++i) {
        const float *a = projected[i], *b = projected[(i + 1) % 3];
        for (j = 0; j < 4; ++j)
            if (mario_ios_segments_intersect_2d(a, b, corners[j], corners[(j + 1) % 4])) return TRUE;
    }
    return FALSE;
}

static s32 mario_ios_portal_has_nearby_obstacle(const PortalFrame *frame) {
    f32 minX = 100000.0f, maxX = -100000.0f, minZ = 100000.0f, maxZ = -100000.0f;
    s32 cornerX, cornerY, dx, dz, partition, dynamic;
    for (cornerY = -1; cornerY <= 1; cornerY += 2) for (cornerX = -1; cornerX <= 1; cornerX += 2) {
        f32 x = frame->position[0] + cornerX * IOS_PORTAL_HALF_WIDTH * frame->right[0] + cornerY * IOS_PORTAL_HALF_HEIGHT * frame->up[0];
        f32 z = frame->position[2] + cornerX * IOS_PORTAL_HALF_WIDTH * frame->right[2] + cornerY * IOS_PORTAL_HALF_HEIGHT * frame->up[2];
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (z < minZ) minZ = z;
        if (z > maxZ) maxZ = z;
    }
    minX -= IOS_PORTAL_SURFACE_PLANE_TOLERANCE; maxX += IOS_PORTAL_SURFACE_PLANE_TOLERANCE;
    minZ -= IOS_PORTAL_SURFACE_PLANE_TOLERANCE; maxZ += IOS_PORTAL_SURFACE_PLANE_TOLERANCE;
    {
        s32 minCellX = (s32)((minX + LEVEL_BOUNDARY_MAX) / CELL_SIZE) - 1;
        s32 maxCellX = (s32)((maxX + LEVEL_BOUNDARY_MAX) / CELL_SIZE) + 1;
        s32 minCellZ = (s32)((minZ + LEVEL_BOUNDARY_MAX) / CELL_SIZE) - 1;
        s32 maxCellZ = (s32)((maxZ + LEVEL_BOUNDARY_MAX) / CELL_SIZE) + 1;
        for (dz = minCellZ; dz <= maxCellZ; ++dz) for (dx = minCellX; dx <= maxCellX; ++dx) {
            if (dx < 0 || dx >= SURFACE_PARTITION_DIM
                || dz < 0 || dz >= SURFACE_PARTITION_DIM) continue;
            for (partition = SPATIAL_PARTITION_FLOORS; partition <= SPATIAL_PARTITION_WALLS; ++partition) {
                for (dynamic = 0; dynamic <= 1; ++dynamic) {
                    struct SurfaceNode *node = (dynamic ? gDynamicSurfacePartition : gStaticSurfacePartition)[dz][dx][partition].next;
                    for (; node != NULL; node = node->next) {
                        struct Surface *surface = node->surface;
                        PortalTriangle triangle;
                        f32 minDepth = 100000.0f, maxDepth = -100000.0f;
                        s32 v;
                        if (mario_ios_surface_matches_portal(surface, frame)) continue;
                        mario_ios_portal_triangle_from_surface(surface, &triangle);
                        for (v = 0; v < 3; ++v) {
                            f32 r, u, depth;
                            portal_project_to_frame(frame, triangle.vertex[v], &r, &u, &depth);
                            if (depth < minDepth) minDepth = depth;
                            if (depth > maxDepth) maxDepth = depth;
                        }
                        if (minDepth > IOS_PORTAL_SURFACE_PLANE_TOLERANCE
                            || maxDepth < -IOS_PORTAL_SURFACE_PLANE_TOLERANCE) continue;
                        if (mario_ios_triangle_overlaps_aperture(frame, &triangle)) return TRUE;
                    }
                }
            }
        }
    }
    return FALSE;
}

static s32 mario_ios_place_portal(s32 portalType) {
    Vec3f rayOrigin, rayDirection, hitPosition, toSurface, preferredUp;
    struct Surface *surface = NULL;
    PortalFrame candidate;
    f32 directionLength, facing;
    float surfaceNormal[3];
    s16 oldCameraCollisionState;
    s32 i;

    mario_ios_validate_portal_area();
    if (!sIOSPortalGunEnabled) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=portal gun disabled", mario_ios_portal_name(portalType));
        mario_ios_portal_feedback(portalType, "rejected: gun disabled");
        return FALSE;
    }
    if (mario_ios_is_riding_yoshi(gMarioState) || mario_ios_is_driving_kart(gMarioState)) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=mounted on a rideable actor", mario_ios_portal_name(portalType));
        mario_ios_portal_feedback(portalType, "rejected while mounted");
        return FALSE;
    }

    /* The Lakitu focus is the center-of-screen aim point. Start there and
     * continue along the camera's forward ray, rather than tracing from the
     * camera through Mario and often hitting the ground at his feet. */
    vec3f_copy(rayOrigin, gLakituState.focus);
    rayDirection[0] = gLakituState.focus[0] - gLakituState.pos[0];
    rayDirection[1] = gLakituState.focus[1] - gLakituState.pos[1];
    rayDirection[2] = gLakituState.focus[2] - gLakituState.pos[2];
    directionLength = portal_length3(rayDirection);
    if (directionLength < 0.001f) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=invalid camera direction", mario_ios_portal_name(portalType));
        mario_ios_portal_feedback(portalType, "rejected: aim direction unavailable");
        return FALSE;
    }
    rayDirection[0] *= 6000.0f / directionLength;
    rayDirection[1] *= 6000.0f / directionLength;
    rayDirection[2] *= 6000.0f / directionLength;

    oldCameraCollisionState = gCheckingSurfaceCollisionsForCamera;
    gCheckingSurfaceCollisionsForCamera = FALSE;
    find_surface_on_ray(rayOrigin, rayDirection, &surface, hitPosition);
    gCheckingSurfaceCollisionsForCamera = oldCameraCollisionState;
    if (surface == NULL) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=no collision hit rayOrigin=(%.1f,%.1f,%.1f) direction=(%.4f,%.4f,%.4f) range=6000", mario_ios_portal_name(portalType), rayOrigin[0], rayOrigin[1], rayOrigin[2], rayDirection[0] / 6000.0f, rayDirection[1] / 6000.0f, rayDirection[2] / 6000.0f);
        mario_ios_portal_feedback(portalType, "rejected: nothing in aim line");
        return FALSE;
    }
    if (surface->flags & SURFACE_FLAG_DYNAMIC) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=dynamic collision surface=%p type=%d; moving-platform anchors are not yet supported", mario_ios_portal_name(portalType), surface, surface->type);
        mario_ios_portal_feedback(portalType, "rejected: moving platform anchor unsupported");
        return FALSE;
    }
    if (surface->type == SURFACE_CAMERA_BOUNDARY) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=camera-boundary collision surface=%p", mario_ios_portal_name(portalType), surface);
        mario_ios_portal_feedback(portalType, "rejected: camera boundary surface");
        return FALSE;
    }

    candidate.position[0] = hitPosition[0];
    candidate.position[1] = hitPosition[1];
    candidate.position[2] = hitPosition[2];
    toSurface[0] = rayOrigin[0] - hitPosition[0];
    toSurface[1] = rayOrigin[1] - hitPosition[1];
    toSurface[2] = rayOrigin[2] - hitPosition[2];
    surfaceNormal[0] = surface->normal.x;
    surfaceNormal[1] = surface->normal.y;
    surfaceNormal[2] = surface->normal.z;
    facing = portal_dot3(toSurface, surfaceNormal);
    preferredUp[0] = 0.0f; preferredUp[1] = 1.0f; preferredUp[2] = 0.0f;
    if (fabsf(surface->normal.y) > 0.92f) {
        preferredUp[0] = rayDirection[0]; preferredUp[1] = 0.0f; preferredUp[2] = rayDirection[2];
    }
    {
        if (facing < 0.0f) { surfaceNormal[0] = -surfaceNormal[0]; surfaceNormal[1] = -surfaceNormal[1]; surfaceNormal[2] = -surfaceNormal[2]; }
        if (!portal_frame_set_basis(&candidate, surfaceNormal, preferredUp)) {
            IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=invalid surface normal=(%.4f,%.4f,%.4f)", mario_ios_portal_name(portalType), surfaceNormal[0], surfaceNormal[1], surfaceNormal[2]);
            mario_ios_portal_feedback(portalType, "rejected: invalid surface normal");
            return FALSE;
        }
    }

    if (!mario_ios_portal_aperture_has_support(&candidate)) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=collision footprint has an edge or gap hit=(%.1f,%.1f,%.1f) normal=(%.3f,%.3f,%.3f) surface=%p surfaceType=%d; adjacent coplanar triangles were checked", mario_ios_portal_name(portalType), hitPosition[0], hitPosition[1], hitPosition[2], candidate.normal[0], candidate.normal[1], candidate.normal[2], surface, surface->type);
        mario_ios_portal_feedback(portalType, "rejected: full opening does not fit this surface");
        return FALSE;
    }
    if (mario_ios_portal_has_nearby_obstacle(&candidate)) {
        IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=nearby collision geometry intersects opening hit=(%.1f,%.1f,%.1f) normal=(%.3f,%.3f,%.3f)", mario_ios_portal_name(portalType), hitPosition[0], hitPosition[1], hitPosition[2], candidate.normal[0], candidate.normal[1], candidate.normal[2]);
        mario_ios_portal_feedback(portalType, "rejected: geometry blocks portal opening");
        return FALSE;
    }

    for (i = 0; i < 2; i++) {
        f32 dx, dy, dz;
        f32 normalAlignment;
        if (i == portalType || !sIOSPortalValid[i]) continue;
        dx = candidate.position[0] - sIOSPortalFrames[i].position[0];
        dy = candidate.position[1] - sIOSPortalFrames[i].position[1];
        dz = candidate.position[2] - sIOSPortalFrames[i].position[2];
        normalAlignment = fabsf(portal_dot3(candidate.normal, sIOSPortalFrames[i].normal));
        if (normalAlignment > 0.90f && dx * dx + dy * dy + dz * dz < 180.0f * 180.0f) {
            IOS_PORTAL_EVENT("Portal placement rejected type=%s reason=overlaps paired portal otherType=%s distance=%.1f", mario_ios_portal_name(portalType), mario_ios_portal_name(i), sqrtf(dx*dx + dy*dy + dz*dz));
            mario_ios_portal_feedback(portalType, "rejected: overlaps the other portal");
            return FALSE;
        }
    }

    sIOSPortalFrames[portalType] = candidate;
    sIOSPortalValid[portalType] = TRUE;
    sIOSPortalObjectVisibleLogged[portalType] = FALSE;
    IOS_PORTAL_EVENT("Portal placement succeeded type=%s level=%d area=%d hit=(%.1f,%.1f,%.1f) normal=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f) surface=%p surfaceType=%d flags=0x%x coverage=full opening=156x360", mario_ios_portal_name(portalType), gCurrLevelNum, gCurrentArea ? gCurrentArea->index : -1, hitPosition[0], hitPosition[1], hitPosition[2], candidate.normal[0], candidate.normal[1], candidate.normal[2], candidate.right[0], candidate.right[1], candidate.right[2], candidate.up[0], candidate.up[1], candidate.up[2], surface, surface->type, surface->flags);
    mario_ios_portal_feedback(portalType, "placed");
    return TRUE;
}

s32 mario_ios_portal_surface_passable(const struct Surface *surface, f32 x, f32 y,
                                      f32 z, f32 radius) {
    struct MarioState *m = gMarioState;
    f32 center[3];
    f32 velocityToward;
    s32 i;

    if (!sIOSPortalGunEnabled || !sIOSPortalValid[0] || !sIOSPortalValid[1]
        || m == NULL || mario_ios_is_driving_kart(m)
        || gCurrentObject != gMarioObject || gCheckingSurfaceCollisionsForCamera)
        return FALSE;

    mario_ios_validate_portal_area();
    if (!sIOSPortalValid[0] || !sIOSPortalValid[1]) return FALSE;

    for (i = 0; i < 2; i++) {
        PortalFrame *frame = &sIOSPortalFrames[i];
        f32 localRight, localUp, localDepth;
        f32 rightExtent, upExtent, normalExtent;
        if (!mario_ios_surface_matches_portal(surface, frame)) continue;
        center[0] = x;
        center[1] = m->pos[1] + IOS_PORTAL_PLAYER_HEIGHT * 0.5f;
        center[2] = z;
        portal_project_to_frame(frame, center, &localRight, &localUp, &localDepth);
        rightExtent = portal_capsule_axis_extent(frame->right, radius, IOS_PORTAL_PLAYER_HEIGHT);
        upExtent = portal_capsule_axis_extent(frame->up, radius, IOS_PORTAL_PLAYER_HEIGHT);
        normalExtent = portal_capsule_axis_extent(frame->normal, radius, IOS_PORTAL_PLAYER_HEIGHT);
        if (fabsf(localDepth) > normalExtent + 10.0f
            || fabsf(localRight) > IOS_PORTAL_HALF_WIDTH - rightExtent
            || fabsf(localUp) > IOS_PORTAL_HALF_HEIGHT - upExtent)
            continue;

        velocityToward = portal_dot3(m->vel, frame->normal);
        if (velocityToward >= -0.5f && m->forwardVel > 0.0f) {
            Vec3f forward = {
                coss(m->faceAngle[0]) * sins(m->faceAngle[1]) * m->forwardVel,
                sins(m->faceAngle[0]) * m->forwardVel,
                coss(m->faceAngle[0]) * coss(m->faceAngle[1]) * m->forwardVel,
            };
            velocityToward = portal_dot3(forward, frame->normal);
        }
        return velocityToward < -0.5f;
    }
    return FALSE;
}

static void mario_ios_transform_camera_point(const PortalFrame *from, const PortalFrame *to,
                                              Vec3f point) {
    Vec3f transformed;
    portal_transform_point(from, to, point, 0.0f, transformed);
    vec3f_copy(point, transformed);
}

static s16 mario_ios_transform_yaw(const PortalFrame *from, const PortalFrame *to, s16 yaw) {
    Vec3f direction = {sins(yaw), 0.0f, coss(yaw)};
    Vec3f transformed;
    portal_transform_vector(from, to, direction, transformed);
    if (transformed[0] * transformed[0] + transformed[2] * transformed[2] < 0.0001f)
        return yaw;
    return atan2s(transformed[0], transformed[2]);
}

static void mario_ios_reject_portal_exit(struct MarioState *m, Vec3f previousPosition) {
    vec3f_copy(m->pos, previousPosition);
    m->vel[0] = m->vel[1] = m->vel[2] = 0.0f;
    mario_set_forward_vel(m, 0.0f);
    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
    m->marioObj->oPosX = m->pos[0];
    m->marioObj->oPosY = m->pos[1];
    m->marioObj->oPosZ = m->pos[2];
}

static s32 mario_ios_try_portal_traversal(struct MarioState *m, Vec3f previousPosition) {
    s32 sourceIndex;
    s32 destinationIndex;
    f32 crossingT;

    mario_ios_validate_portal_area();
    if (!sIOSPortalGunEnabled || !sIOSPortalValid[0] || !sIOSPortalValid[1]
        || m == NULL || mario_ios_is_riding_yoshi(m) || mario_ios_is_driving_kart(m)
        || sIOSPortalTraversalCooldown > 0)
        return FALSE;

    for (sourceIndex = 0; sourceIndex < 2; sourceIndex++) {
        PortalFrame *from = &sIOSPortalFrames[sourceIndex];
        PortalFrame *to;
        Vec3f previousCenter;
        Vec3f currentCenter;
        Vec3f transformedCenter;
        Vec3f transformedPosition;
        Vec3f transformedVelocity;
        Vec3f transformedSlide;
        Vec3f attemptedPosition;
        Vec3f requestedPosition;
        f32 destinationSupport;
        s32 wasAirborne = (m->action & ACT_FLAG_AIR) != 0;
        struct Surface *floor = NULL;
        struct Surface *ceil = NULL;
        f32 floorHeight;
        f32 ceilHeight;

        vec3f_copy(previousCenter, previousPosition);
        vec3f_copy(currentCenter, m->pos);
        previousCenter[1] += IOS_PORTAL_PLAYER_HEIGHT * 0.5f;
        currentCenter[1] += IOS_PORTAL_PLAYER_HEIGHT * 0.5f;
        if (!portal_segment_crosses(from, previousCenter, currentCenter,
                                    IOS_PORTAL_HALF_WIDTH, IOS_PORTAL_HALF_HEIGHT,
                                    IOS_PORTAL_PLAYER_RADIUS, IOS_PORTAL_PLAYER_HEIGHT,
                                    &crossingT)) continue;

        destinationIndex = 1 - sourceIndex;
        to = &sIOSPortalFrames[destinationIndex];
        destinationSupport = portal_capsule_axis_extent(to->normal, IOS_PORTAL_PLAYER_RADIUS,
                                                        IOS_PORTAL_PLAYER_HEIGHT)
                           + IOS_PORTAL_EXIT_CLEARANCE;
        IOS_PORTAL_EVENT("Portal traversal crossing source=%s destination=%s t=%.3f center=(%.1f,%.1f,%.1f) velocity=(%.1f,%.1f,%.1f)", mario_ios_portal_name(sourceIndex), mario_ios_portal_name(destinationIndex), crossingT, currentCenter[0], currentCenter[1], currentCenter[2], m->vel[0], m->vel[1], m->vel[2]);
        portal_transform_point(from, to, currentCenter, destinationSupport, transformedCenter);
        vec3f_copy(requestedPosition, transformedCenter);
        requestedPosition[1] -= IOS_PORTAL_PLAYER_HEIGHT * 0.5f;
        vec3f_copy(attemptedPosition, requestedPosition);
        /* Resolve the destination against unchanged level collision. Small
         * collision nudges are acceptable; a large correction means the exit
         * would put Mario inside another wall. */
        f32_find_wall_collision(&attemptedPosition[0], &attemptedPosition[1],
                                &attemptedPosition[2], 80.0f,
                                IOS_PORTAL_PLAYER_RADIUS);
        floorHeight = find_floor(attemptedPosition[0], attemptedPosition[1] + IOS_PORTAL_PLAYER_HEIGHT,
                                 attemptedPosition[2], &floor);
        if (!portal_exit_correction_is_safe(requestedPosition, attemptedPosition,
                                            IOS_PORTAL_PLAYER_RADIUS * 2.0f)
            || !portal_exit_resolve_floor(&attemptedPosition[1], floorHeight,
                                          floor != NULL, wasAirborne,
                                          IOS_PORTAL_GROUND_SNAP_MAX)) {
            IOS_PORTAL_EVENT("Portal traversal rejected source=%s destination=%s reason=wall/floor correction unsafe requested=(%.1f,%.1f,%.1f) resolved=(%.1f,%.1f,%.1f) floor=%s floorY=%.1f", mario_ios_portal_name(sourceIndex), mario_ios_portal_name(destinationIndex), requestedPosition[0], requestedPosition[1], requestedPosition[2], attemptedPosition[0], attemptedPosition[1], attemptedPosition[2], floor ? "yes" : "no", floorHeight);
            mario_ios_portal_feedback(sourceIndex, "exit blocked; move the other portal to open ground");
            mario_ios_reject_portal_exit(m, previousPosition);
            return TRUE;
        }
        ceilHeight = find_ceil(attemptedPosition[0], attemptedPosition[1] + 80.0f,
                               attemptedPosition[2], &ceil);
        if (ceil != NULL && ceilHeight < attemptedPosition[1] + IOS_PORTAL_PLAYER_HEIGHT) {
            IOS_PORTAL_EVENT("Portal traversal rejected source=%s destination=%s reason=ceiling blocks exit ceilingY=%.1f requestedFeetY=%.1f", mario_ios_portal_name(sourceIndex), mario_ios_portal_name(destinationIndex), ceilHeight, attemptedPosition[1]);
            mario_ios_portal_feedback(sourceIndex, "exit blocked by ceiling");
            mario_ios_reject_portal_exit(m, previousPosition);
            return TRUE;
        }
        vec3f_copy(transformedPosition, attemptedPosition);
        portal_transform_vector(from, to, m->vel, transformedVelocity);
        {
            Vec3f slide = {m->slideVelX, 0.0f, m->slideVelZ};
            portal_transform_vector(from, to, slide, transformedSlide);
        }

        vec3f_copy(m->pos, transformedPosition);
        if (!wasAirborne && m->pos[1] < floorHeight) m->pos[1] = floorHeight;
        vec3f_copy(m->vel, transformedVelocity);
        m->slideVelX = transformedSlide[0];
        m->slideVelZ = transformedSlide[2];
        m->faceAngle[1] = mario_ios_transform_yaw(from, to, m->faceAngle[1]);
        m->intendedYaw = mario_ios_transform_yaw(from, to, m->intendedYaw);
        m->slideYaw = mario_ios_transform_yaw(from, to, m->slideYaw);
        m->forwardVel = sins(m->faceAngle[1]) * m->vel[0]
                      + coss(m->faceAngle[1]) * m->vel[2];

        /* All camera history is transformed in the same frame as Mario to
         * prevent the normal smoothing state from snapping back across worlds. */
        mario_ios_transform_camera_point(from, to, gLakituState.pos);
        mario_ios_transform_camera_point(from, to, gLakituState.focus);
        mario_ios_transform_camera_point(from, to, gLakituState.curPos);
        mario_ios_transform_camera_point(from, to, gLakituState.curFocus);
        mario_ios_transform_camera_point(from, to, gLakituState.goalPos);
        mario_ios_transform_camera_point(from, to, gLakituState.goalFocus);
        if (gCamera != NULL) {
            mario_ios_transform_camera_point(from, to, gCamera->pos);
            mario_ios_transform_camera_point(from, to, gCamera->focus);
            gCamera->yaw = atan2s(gCamera->pos[0] - gCamera->focus[0],
                                  gCamera->pos[2] - gCamera->focus[2]);
            gCamera->nextYaw = mario_ios_transform_yaw(from, to, gCamera->nextYaw);
        }

        /* Reuse the safely resolved destination support surfaces. */
        m->floor = floor;
        m->floorHeight = floorHeight;
        m->ceilHeight = find_ceil(m->pos[0], m->pos[1] + 160.0f, m->pos[2], &ceil);
        m->ceil = ceil;
        m->wall = NULL;

        vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
        m->marioObj->oPosX = m->pos[0];
        m->marioObj->oPosY = m->pos[1];
        m->marioObj->oPosZ = m->pos[2];
        vec3s_set(m->marioObj->header.gfx.angle, 0, m->faceAngle[1], 0);
        sIOSPortalTraversalCooldown = 6;
        IOS_PORTAL_EVENT("Portal traversal succeeded source=%s destination=%s position=(%.1f,%.1f,%.1f) velocity=(%.1f,%.1f,%.1f) action=0x%08x floor=%p", mario_ios_portal_name(sourceIndex), mario_ios_portal_name(destinationIndex), m->pos[0], m->pos[1], m->pos[2], m->vel[0], m->vel[1], m->vel[2], m->action, floor);
        mario_ios_portal_feedback(destinationIndex, "traversal successful");
        return TRUE;
    }
    return FALSE;
}

void mario_ios_sync_portal_object(struct Object *obj, s32 portalType) {
    PortalFrame *frame;
    Vec3s angle;
    f32 pulse;

    if (obj == NULL || portalType < MARIO_IOS_PORTAL_BLUE || portalType > MARIO_IOS_PORTAL_ORANGE)
        return;
    if (!sIOSPortalValid[portalType]) {
        obj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
        if (sIOSPortalObjectVisibleLogged[portalType]) {
            IOS_PORTAL_EVENT("Portal actor hidden type=%s reason=portal state cleared; display-list object remains resident", mario_ios_portal_name(portalType));
            sIOSPortalObjectVisibleLogged[portalType] = FALSE;
        }
        return;
    }

    frame = &sIOSPortalFrames[portalType];
    {
        f32 horizontal = sqrtf(frame->normal[0] * frame->normal[0] + frame->normal[2] * frame->normal[2]);
        angle[0] = atan2s(-frame->normal[1], horizontal);
        angle[1] = atan2s(frame->normal[0], frame->normal[2]);
        /* Rotate the zero-roll local up vector around the portal normal until
         * it matches the frame chosen during placement. */
        {
            f32 sx = sins(angle[0]), cx = coss(angle[0]);
            f32 sy = sins(angle[1]), cy = coss(angle[1]);
            f32 baseRight[3] = {cy, 0.0f, -sy};
            f32 baseUp[3] = {sx * sy, cx, sx * cy};
            angle[2] = atan2s(-portal_dot3(frame->up, baseRight),
                               portal_dot3(frame->up, baseUp));
        }
    }
    obj->oPosX = frame->position[0] + frame->normal[0] * IOS_PORTAL_VISUAL_OFFSET;
    obj->oPosY = frame->position[1] + frame->normal[1] * IOS_PORTAL_VISUAL_OFFSET;
    obj->oPosZ = frame->position[2] + frame->normal[2] * IOS_PORTAL_VISUAL_OFFSET;
    obj_set_gfx_pos_from_pos(obj);
    obj_set_angle(obj, angle[0], angle[1], angle[2]);
    pulse = 1.0f + 0.025f * sins((s16)(gGlobalTimer * 0x0800));
    obj_scale(obj, pulse);
    obj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
    if (!sIOSPortalObjectVisibleLogged[portalType]) {
        IOS_PORTAL_EVENT("Portal actor visible type=%s model=%s modelId=%d position=(%.1f,%.1f,%.1f) normal=(%.3f,%.3f,%.3f) angles=(%d,%d,%d) object=%p; ordinary geo/display-list submission is now enabled", mario_ios_portal_name(portalType), portalType == MARIO_IOS_PORTAL_BLUE ? "blue" : "orange", portalType == MARIO_IOS_PORTAL_BLUE ? MODEL_PORTAL_BLUE : MODEL_PORTAL_ORANGE, obj->oPosX, obj->oPosY, obj->oPosZ, frame->normal[0], frame->normal[1], frame->normal[2], angle[0], angle[1], angle[2], obj);
        sIOSPortalObjectVisibleLogged[portalType] = TRUE;
    }
}

static s32 mario_ios_spawn_reality_actor(struct MarioState *m, s32 spawnBobomb) {
    const BehaviorScript *behavior = segmented_to_virtual(spawnBobomb ? bhvBobomb : bhvGoomba);
    struct Object *object = NULL;
    struct Surface *spawnFloor = NULL;
    f32 spawnX, spawnY, spawnZ;
    s32 i, freeSlot = -1;
    f32 forwardX = sins(m->faceAngle[1]);
    f32 forwardZ = coss(m->faceAngle[1]);

    spawnX = m->pos[0] + forwardX * 180.0f;
    spawnZ = m->pos[2] + forwardZ * 180.0f;
    spawnY = find_floor(spawnX, m->floorHeight + 150.0f, spawnZ, &spawnFloor);
    if (spawnFloor == NULL || spawnY < m->floorHeight - 200.0f || spawnY > m->floorHeight + 200.0f) {
        IOS_REALITY_STATUS("No safe floor ahead to spawn actor");
        IOS_REALITY_EVENT("Reality actor spawn rejected: no nearby floor at (%.1f,%.1f) currentFloor=%.1f", spawnX, spawnZ, m->floorHeight);
        return FALSE;
    }

    if (sIOSRealitySpawnArea != gCurrentArea) {
        memset(sIOSRealitySpawnedObjects, 0, sizeof(sIOSRealitySpawnedObjects));
        sIOSRealitySpawnArea = gCurrentArea;
    }
    for (i = 0; i < (s32)(sizeof(sIOSRealitySpawnedObjects) / sizeof(sIOSRealitySpawnedObjects[0])); i++) {
        struct Object *tracked = sIOSRealitySpawnedObjects[i].object;
        if (tracked && (!(tracked->activeFlags & ACTIVE_FLAG_ACTIVE)
                        || tracked->behavior != sIOSRealitySpawnedObjects[i].behavior
                        || tracked->oHomeX != sIOSRealitySpawnedObjects[i].homeX
                        || tracked->oHomeY != sIOSRealitySpawnedObjects[i].homeY
                        || tracked->oHomeZ != sIOSRealitySpawnedObjects[i].homeZ)) {
            sIOSRealitySpawnedObjects[i].object = NULL;
            sIOSRealitySpawnedObjects[i].behavior = NULL;
        }
        if (sIOSRealitySpawnedObjects[i].object == NULL && freeSlot < 0) freeSlot = i;
    }
    if (freeSlot < 0) {
        IOS_REALITY_STATUS("Reality Debugger spawn limit reached; clear spawned actors first");
        IOS_REALITY_EVENT("Reality actor spawn rejected: tracked actor limit=%u", (unsigned)(sizeof(sIOSRealitySpawnedObjects) / sizeof(sIOSRealitySpawnedObjects[0])));
        return FALSE;
    }
    object = spawn_object(m->marioObj, spawnBobomb ? MODEL_BLACK_BOBOMB : MODEL_GOOMBA, behavior);
    if (object == NULL) {
        IOS_REALITY_STATUS("Reality Debugger could not allocate actor");
        IOS_REALITY_EVENT("Reality actor spawn rejected: object pool exhausted type=%s", spawnBobomb ? "bob-omb" : "goomba");
        return FALSE;
    }
    object->oPosX = spawnX;
    object->oPosY = spawnY;
    object->oPosZ = spawnZ;
    object->oHomeX = object->oPosX;
    object->oHomeY = object->oPosY;
    object->oHomeZ = object->oPosZ;
    object->oMoveAngleYaw = m->faceAngle[1];
    object->oFaceAngleYaw = m->faceAngle[1];
    obj_set_gfx_pos_from_pos(object);
    sIOSRealitySpawnedObjects[freeSlot].object = object;
    sIOSRealitySpawnedObjects[freeSlot].behavior = behavior;
    sIOSRealitySpawnedObjects[freeSlot].homeX = object->oHomeX;
    sIOSRealitySpawnedObjects[freeSlot].homeY = object->oHomeY;
    sIOSRealitySpawnedObjects[freeSlot].homeZ = object->oHomeZ;
    IOS_REALITY_EVENT("Reality actor spawned type=%s object=%p position=(%.1f,%.1f,%.1f) area=%d", spawnBobomb ? "bob-omb" : "goomba", object, object->oPosX, object->oPosY, object->oPosZ, gCurrentArea ? gCurrentArea->index : -1);
    IOS_REALITY_STATUS(spawnBobomb ? "Bob-omb spawned ahead" : "Goomba spawned ahead");
    return TRUE;
}

static void mario_ios_clear_reality_actors(void) {
    s32 i;
    if (sIOSRealitySpawnArea != gCurrentArea) {
        memset(sIOSRealitySpawnedObjects, 0, sizeof(sIOSRealitySpawnedObjects));
        sIOSRealitySpawnArea = gCurrentArea;
        IOS_REALITY_EVENT("Reality actor tracking reset after area transition area=%d", gCurrentArea ? gCurrentArea->index : -1);
        return;
    }
    for (i = 0; i < (s32)(sizeof(sIOSRealitySpawnedObjects) / sizeof(sIOSRealitySpawnedObjects[0])); i++) {
        struct Object *object = sIOSRealitySpawnedObjects[i].object;
        if (object && (object->activeFlags & ACTIVE_FLAG_ACTIVE)
            && object->behavior == sIOSRealitySpawnedObjects[i].behavior
            && object->oHomeX == sIOSRealitySpawnedObjects[i].homeX
            && object->oHomeY == sIOSRealitySpawnedObjects[i].homeY
            && object->oHomeZ == sIOSRealitySpawnedObjects[i].homeZ) {
            mark_obj_for_deletion(object);
        }
        sIOSRealitySpawnedObjects[i].object = NULL;
        sIOSRealitySpawnedObjects[i].behavior = NULL;
    }
    IOS_REALITY_EVENT("Reality spawned actors marked for cleanup area=%d", gCurrentArea ? gCurrentArea->index : -1);
    IOS_REALITY_STATUS("Spawned debugger actors cleared");
}

static void mario_ios_process_reality_actions(struct MarioState *m) {
    u32 count;
    if ((count = ios_reality_take_action_requests(IOS_REALITY_SPAWN_GOOMBA)) != 0) {
        while (count--) mario_ios_spawn_reality_actor(m, FALSE);
    }
    if ((count = ios_reality_take_action_requests(IOS_REALITY_SPAWN_BOBOMB)) != 0) {
        while (count--) mario_ios_spawn_reality_actor(m, TRUE);
    }
    if (ios_reality_take_action_requests(IOS_REALITY_CLEAR_SPAWNED_OBJECTS))
        mario_ios_clear_reality_actors();
}

static void mario_ios_apply_gameplay_requests(struct MarioState *m) {
    u32 requests = __atomic_exchange_n(&sIOSGameplayRequests, 0, __ATOMIC_ACQUIRE);

    if (requests & (IOS_GAMEPLAY_REQUEST_PORTAL_BLUE | IOS_GAMEPLAY_REQUEST_PORTAL_ORANGE
                    | IOS_GAMEPLAY_REQUEST_PORTAL_ENABLE | IOS_GAMEPLAY_REQUEST_PORTAL_RESET)) {
        IOS_PORTAL_EVENT("Portal requests consumed by game thread mask=0x%02x action=0x%08x area=%d mario=(%.1f,%.1f,%.1f)", requests, m->action, gCurrentArea ? gCurrentArea->index : -1, m->pos[0], m->pos[1], m->pos[2]);
    }

    if ((requests & IOS_GAMEPLAY_REQUEST_CHARACTER) && !mario_ios_is_driving_kart(m)) {
        sTemporaryLuigiPaletteEnabled = !sTemporaryLuigiPaletteEnabled;
        mario_set_temporary_luigi_palette(sTemporaryLuigiPaletteEnabled);
    } else if (requests & IOS_GAMEPLAY_REQUEST_CHARACTER) {
        IOS_KART_STATUS("Dismount before switching drivers");
    }

    if (requests & IOS_GAMEPLAY_REQUEST_YOSHI) {
        if (mario_ios_is_riding_yoshi(m)) {
            s32 wasAirborne = m->action == ACT_RIDING_YOSHI_AIR;
            mario_ios_detach_yoshi(m);
            set_mario_action(m, wasAirborne ? ACT_FREEFALL : ACT_WALKING, 0);
        } else {
            mario_ios_try_mount_yoshi(m);
        }
    }

    if (requests & IOS_GAMEPLAY_REQUEST_KART_SPAWN) {
        if (mario_ios_is_driving_kart(m)) {
            IOS_KART_STATUS("Already driving");
        } else {
            mario_ios_spawn_or_reposition_kart(m);
        }
    }

    if (requests & IOS_GAMEPLAY_REQUEST_PORTAL_ENABLE) {
        sIOSPortalGunEnabled = !sIOSPortalGunEnabled;
        IOS_PORTAL_EVENT("Portal gun %s", sIOSPortalGunEnabled ? "enabled" : "disabled");
        mario_ios_portal_feedback(MARIO_IOS_PORTAL_BLUE, sIOSPortalGunEnabled ? "gun enabled" : "gun disabled");
    }
    mario_ios_validate_portal_area();
    if (requests & IOS_GAMEPLAY_REQUEST_PORTAL_RESET) {
        mario_ios_reset_portals();
        sIOSPortalObjectVisibleLogged[0] = sIOSPortalObjectVisibleLogged[1] = FALSE;
        IOS_PORTAL_EVENT("Both portals reset area=%d", gCurrentArea ? gCurrentArea->index : -1);
        mario_ios_portal_feedback(MARIO_IOS_PORTAL_BLUE, "reset");
    }
    if (requests & IOS_GAMEPLAY_REQUEST_PORTAL_BLUE)
        mario_ios_place_portal(MARIO_IOS_PORTAL_BLUE);
    if (requests & IOS_GAMEPLAY_REQUEST_PORTAL_ORANGE)
        mario_ios_place_portal(MARIO_IOS_PORTAL_ORANGE);
}

/**************************************************
 *                    ANIMATIONS                  *
 **************************************************/

/**
 * Checks if Mario's animation has reached its end point.
 */
s32 is_anim_at_end(struct MarioState *m) {
    struct Object *o = m->marioObj;

    return (o->header.gfx.unk38.animFrame + 1) == o->header.gfx.unk38.curAnim->unk08;
}

/**
 * Checks if Mario's animation has surpassed 2 frames before its end point.
 */
s32 is_anim_past_end(struct MarioState *m) {
    struct Object *o = m->marioObj;

    return o->header.gfx.unk38.animFrame >= (o->header.gfx.unk38.curAnim->unk08 - 2);
}

/**
 * Sets Mario's animation without any acceleration, running at its default rate.
 */
s16 set_mario_animation(struct MarioState *m, s32 targetAnimID) {
    struct Object *o = m->marioObj;
    struct Animation *targetAnim = m->animation->targetAnim;

    if (load_patchable_table(m->animation, targetAnimID)) {
        targetAnim->values = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->values);
        targetAnim->index = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->index);
    }

    if (o->header.gfx.unk38.animID != targetAnimID) {
        o->header.gfx.unk38.animID = targetAnimID;
        o->header.gfx.unk38.curAnim = targetAnim;
        o->header.gfx.unk38.animAccel = 0;
        o->header.gfx.unk38.animYTrans = m->unkB0;

        if (targetAnim->flags & ANIM_FLAG_2) {
            o->header.gfx.unk38.animFrame = targetAnim->unk04;
        } else {
            if (targetAnim->flags & ANIM_FLAG_FORWARD) {
                o->header.gfx.unk38.animFrame = targetAnim->unk04 + 1;
            } else {
                o->header.gfx.unk38.animFrame = targetAnim->unk04 - 1;
            }
        }
    }

    return o->header.gfx.unk38.animFrame;
}

/**
 * Sets Mario's animation where the animation is sped up or
 * slowed down via acceleration.
 */
s16 set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel) {
    struct Object *o = m->marioObj;
    struct Animation *targetAnim = m->animation->targetAnim;

    if (load_patchable_table(m->animation, targetAnimID)) {
        targetAnim->values = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->values);
        targetAnim->index = (void *) VIRTUAL_TO_PHYSICAL((u8 *) targetAnim + (uintptr_t) targetAnim->index);
    }

    if (o->header.gfx.unk38.animID != targetAnimID) {
        o->header.gfx.unk38.animID = targetAnimID;
        o->header.gfx.unk38.curAnim = targetAnim;
        o->header.gfx.unk38.animYTrans = m->unkB0;

        if (targetAnim->flags & ANIM_FLAG_2) {
            o->header.gfx.unk38.animFrameAccelAssist = (targetAnim->unk04 << 0x10);
        } else {
            if (targetAnim->flags & ANIM_FLAG_FORWARD) {
                o->header.gfx.unk38.animFrameAccelAssist = (targetAnim->unk04 << 0x10) + accel;
            } else {
                o->header.gfx.unk38.animFrameAccelAssist = (targetAnim->unk04 << 0x10) - accel;
            }
        }

        o->header.gfx.unk38.animFrame = (o->header.gfx.unk38.animFrameAccelAssist >> 0x10);
    }

    o->header.gfx.unk38.animAccel = accel;

    return o->header.gfx.unk38.animFrame;
}

/**
 * Sets the animation to a specific "next" frame from the frame given.
 */
void set_anim_to_frame(struct MarioState *m, s16 animFrame) {
    struct GraphNodeObject_sub *animInfo = &m->marioObj->header.gfx.unk38;
    struct Animation *curAnim = animInfo->curAnim;

    if (animInfo->animAccel) {
        if (curAnim->flags & ANIM_FLAG_FORWARD) {
            animInfo->animFrameAccelAssist = (animFrame << 0x10) + animInfo->animAccel;
        } else {
            animInfo->animFrameAccelAssist = (animFrame << 0x10) - animInfo->animAccel;
        }
    } else {
        if (curAnim->flags & ANIM_FLAG_FORWARD) {
            animInfo->animFrame = animFrame + 1;
        } else {
            animInfo->animFrame = animFrame - 1;
        }
    }
}

s32 is_anim_past_frame(struct MarioState *m, s16 animFrame) {
    s32 isPastFrame;
    s32 acceleratedFrame = animFrame << 0x10;
    struct GraphNodeObject_sub *animInfo = &m->marioObj->header.gfx.unk38;
    struct Animation *curAnim = animInfo->curAnim;

    if (animInfo->animAccel) {
        if (curAnim->flags & ANIM_FLAG_FORWARD) {
            isPastFrame =
                (animInfo->animFrameAccelAssist > acceleratedFrame)
                && (acceleratedFrame >= (animInfo->animFrameAccelAssist - animInfo->animAccel));
        } else {
            isPastFrame =
                (animInfo->animFrameAccelAssist < acceleratedFrame)
                && (acceleratedFrame <= (animInfo->animFrameAccelAssist + animInfo->animAccel));
        }
    } else {
        if (curAnim->flags & ANIM_FLAG_FORWARD) {
            isPastFrame = (animInfo->animFrame == (animFrame + 1));
        } else {
            isPastFrame = ((animInfo->animFrame + 1) == animFrame);
        }
    }

    return isPastFrame;
}

/**
 * Rotates the animation's translation into the global coordinate system
 * and returns the animation's flags.
 */
s16 find_mario_anim_flags_and_translation(struct Object *obj, s32 yaw, Vec3s translation) {
    f32 dx;
    f32 dz;

    struct Animation *curAnim = (void *) obj->header.gfx.unk38.curAnim;
    s16 animFrame = geo_update_animation_frame(&obj->header.gfx.unk38, NULL);
    u16 *animIndex = segmented_to_virtual((void *) curAnim->index);
    s16 *animValues = segmented_to_virtual((void *) curAnim->values);

    f32 s = (f32) sins(yaw);
    f32 c = (f32) coss(yaw);

    dx = *(animValues + (retrieve_animation_index(animFrame, &animIndex))) / 4.0f;
    translation[1] = *(animValues + (retrieve_animation_index(animFrame, &animIndex))) / 4.0f;
    dz = *(animValues + (retrieve_animation_index(animFrame, &animIndex))) / 4.0f;

    translation[0] = (dx * c) + (dz * s);
    translation[2] = (-dx * s) + (dz * c);

    return curAnim->flags;
}

/**
 * Updates Mario's position from his animation's translation.
 */
void update_mario_pos_for_anim(struct MarioState *m) {
    Vec3s translation;
    s16 flags;

    flags = find_mario_anim_flags_and_translation(m->marioObj, m->faceAngle[1], translation);

    if (flags & (ANIM_FLAG_HOR_TRANS | ANIM_FLAG_6)) {
        m->pos[0] += (f32) translation[0];
        m->pos[2] += (f32) translation[2];
    }

    if (flags & (ANIM_FLAG_VERT_TRANS | ANIM_FLAG_6)) {
        m->pos[1] += (f32) translation[1];
    }
}

/**
 * Finds the vertical translation from Mario's animation.
 */
s16 return_mario_anim_y_translation(struct MarioState *m) {
    Vec3s translation;
    find_mario_anim_flags_and_translation(m->marioObj, 0, translation);

    return translation[1];
}

/**************************************************
 *                      AUDIO                     *
 **************************************************/

/**
 * Plays a sound if if Mario doesn't have the flag being checked.
 */
void play_sound_if_no_flag(struct MarioState *m, u32 soundBits, u32 flags) {
    if ((m->flags & flags) == 0) {
        play_sound(soundBits, m->marioObj->header.gfx.cameraToObject);
        m->flags |= flags;
    }
}

/**
 * Plays a jump sound if one has not been played since the last action change.
 */
void play_mario_jump_sound(struct MarioState *m) {
    if (!(m->flags & MARIO_MARIO_SOUND_PLAYED)) {
#ifndef VERSION_JP
        if (m->action == ACT_TRIPLE_JUMP) {
            play_sound(SOUND_MARIO_YAHOO_WAHA_YIPPEE + ((gAudioRandom % 5) << 16),
                       m->marioObj->header.gfx.cameraToObject);
        } else {
#endif
            play_sound(SOUND_MARIO_YAH_WAH_HOO + ((gAudioRandom % 3) << 16),
                       m->marioObj->header.gfx.cameraToObject);
#ifndef VERSION_JP
        }
#endif

        m->flags |= MARIO_MARIO_SOUND_PLAYED;
    }
}

/**
 * Adjusts the volume/pitch of sounds from Mario's speed.
 */
void adjust_sound_for_speed(struct MarioState *m) {
    s32 absForwardVel = (m->forwardVel > 0.0f) ? m->forwardVel : -m->forwardVel;
    func_80320A4C(1, (absForwardVel > 100) ? 100 : absForwardVel);
}

/**
 * Spawns particles if the step sound says to, then either plays a step sound or relevant other sound.
 */
void play_sound_and_spawn_particles(struct MarioState *m, u32 soundBits, u32 waveParticleType) {
    if (m->terrainSoundAddend == (SOUND_TERRAIN_WATER << 16)) {
        if (waveParticleType != 0) {
            m->particleFlags |= PARTICLE_SHALLOW_WATER_SPLASH;
        } else {
            m->particleFlags |= PARTICLE_SHALLOW_WATER_WAVE;
        }
    } else {
        if (m->terrainSoundAddend == (SOUND_TERRAIN_SAND << 16)) {
            m->particleFlags |= PARTICLE_DIRT;
        } else if (m->terrainSoundAddend == (SOUND_TERRAIN_SNOW << 16)) {
            m->particleFlags |= PARTICLE_SNOW;
        }
    }

    if ((m->flags & MARIO_METAL_CAP) || soundBits == SOUND_ACTION_UNSTUCK_FROM_GROUND
        || soundBits == SOUND_MARIO_PUNCH_HOO) {
        play_sound(soundBits, m->marioObj->header.gfx.cameraToObject);
    } else {
        play_sound(m->terrainSoundAddend + soundBits, m->marioObj->header.gfx.cameraToObject);
    }
}

/**
 * Plays an environmental sound if one has not been played since the last action change.
 */
void play_mario_action_sound(struct MarioState *m, u32 soundBits, u32 waveParticleType) {
    if ((m->flags & MARIO_ACTION_SOUND_PLAYED) == 0) {
        play_sound_and_spawn_particles(m, soundBits, waveParticleType);
        m->flags |= MARIO_ACTION_SOUND_PLAYED;
    }
}

/**
 * Plays a landing sound, accounting for metal cap.
 */
void play_mario_landing_sound(struct MarioState *m, u32 soundBits) {
    play_sound_and_spawn_particles(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_LANDING : soundBits, 1);
}

/**
 * Plays a landing sound, accounting for metal cap. Unlike play_mario_landing_sound,
 * this function uses play_mario_action_sound, making sure the sound is only
 * played once per action.
 */
void play_mario_landing_sound_once(struct MarioState *m, u32 soundBits) {
    play_mario_action_sound(m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_LANDING : soundBits,
                            1);
}

/**
 * Plays a heavy landing (ground pound, etc.) sound, accounting for metal cap.
 */
void play_mario_heavy_landing_sound(struct MarioState *m, u32 soundBits) {
    play_sound_and_spawn_particles(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_HEAVY_LANDING : soundBits, 1);
}

/**
 * Plays a heavy landing (ground pound, etc.) sound, accounting for metal cap.
 * Unlike play_mario_heavy_landing_sound, this function uses play_mario_action_sound,
 * making sure the sound is only played once per action.
 */
void play_mario_heavy_landing_sound_once(struct MarioState *m, u32 soundBits) {
    play_mario_action_sound(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_HEAVY_LANDING : soundBits, 1);
}

/**
 * Plays action and Mario sounds relevant to what was passed into the function.
 */
void play_mario_sound(struct MarioState *m, s32 actionSound, s32 marioSound) {
    if (actionSound == SOUND_ACTION_TERRAIN_JUMP) {
        play_mario_action_sound(
                m, (m->flags & MARIO_METAL_CAP) ? (s32)SOUND_ACTION_METAL_JUMP
                                                : (s32)SOUND_ACTION_TERRAIN_JUMP, 1);
    } else {
        play_sound_if_no_flag(m, actionSound, MARIO_ACTION_SOUND_PLAYED);
    }

    if (marioSound == 0) {
        play_mario_jump_sound(m);
    }

    if (marioSound != -1) {
        play_sound_if_no_flag(m, marioSound, MARIO_MARIO_SOUND_PLAYED);
    }
}

/**************************************************
 *                     ACTIONS                    *
 **************************************************/

/**
 * Sets Mario's other velocities from his forward speed.
 */
void mario_set_forward_vel(struct MarioState *m, f32 forwardVel) {
    m->forwardVel = forwardVel;

    m->slideVelX = sins(m->faceAngle[1]) * m->forwardVel;
    m->slideVelZ = coss(m->faceAngle[1]) * m->forwardVel;

    m->vel[0] = (f32) m->slideVelX;
    m->vel[2] = (f32) m->slideVelZ;
}

/**
 * Returns the slipperiness class of Mario's floor.
 */
s32 mario_get_floor_class(struct MarioState *m) {
    s32 floorClass;

    // The slide terrain type defaults to slide slipperiness.
    // This doesn't matter too much since normally the slide terrain
    // is checked for anyways.
    if ((m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE) {
        floorClass = SURFACE_CLASS_VERY_SLIPPERY;
    } else {
        floorClass = SURFACE_CLASS_DEFAULT;
    }

    if (m->floor) {
        switch (m->floor->type) {
            case SURFACE_NOT_SLIPPERY:
            case SURFACE_HARD_NOT_SLIPPERY:
            case SURFACE_SWITCH:
                floorClass = SURFACE_CLASS_NOT_SLIPPERY;
                break;

            case SURFACE_SLIPPERY:
            case SURFACE_NOISE_SLIPPERY:
            case SURFACE_HARD_SLIPPERY:
            case SURFACE_NO_CAM_COL_SLIPPERY:
                floorClass = SURFACE_CLASS_SLIPPERY;
                break;

            case SURFACE_VERY_SLIPPERY:
            case SURFACE_ICE:
            case SURFACE_HARD_VERY_SLIPPERY:
            case SURFACE_NOISE_VERY_SLIPPERY_73:
            case SURFACE_NOISE_VERY_SLIPPERY_74:
            case SURFACE_NOISE_VERY_SLIPPERY:
            case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
                floorClass = SURFACE_CLASS_VERY_SLIPPERY;
                break;
        }
    }

    // Crawling allows Mario to not slide on certain steeper surfaces.
    if (m->action == ACT_CRAWLING && m->floor->normal.y > 0.5f && floorClass == SURFACE_CLASS_DEFAULT) {
        floorClass = SURFACE_CLASS_NOT_SLIPPERY;
    }

    return floorClass;
}

// clang-format off
s8 sTerrainSounds[7][6] = {
    // default,              hard,                 slippery,
    // very slippery,        noisy default,        noisy slippery
    { SOUND_TERRAIN_DEFAULT, SOUND_TERRAIN_STONE,  SOUND_TERRAIN_GRASS,
      SOUND_TERRAIN_GRASS,   SOUND_TERRAIN_GRASS,  SOUND_TERRAIN_DEFAULT }, // TERRAIN_GRASS
    { SOUND_TERRAIN_STONE,   SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE,
      SOUND_TERRAIN_STONE,   SOUND_TERRAIN_GRASS,  SOUND_TERRAIN_GRASS }, // TERRAIN_STONE
    { SOUND_TERRAIN_SNOW,    SOUND_TERRAIN_ICE,    SOUND_TERRAIN_SNOW,
      SOUND_TERRAIN_ICE,     SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE }, // TERRAIN_SNOW
    { SOUND_TERRAIN_SAND,    SOUND_TERRAIN_STONE,  SOUND_TERRAIN_SAND,
      SOUND_TERRAIN_SAND,    SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE }, // TERRAIN_SAND
    { SOUND_TERRAIN_SPOOKY,  SOUND_TERRAIN_SPOOKY, SOUND_TERRAIN_SPOOKY,
      SOUND_TERRAIN_SPOOKY,  SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE }, // TERRAIN_SPOOKY
    { SOUND_TERRAIN_DEFAULT, SOUND_TERRAIN_STONE,  SOUND_TERRAIN_GRASS,
      SOUND_TERRAIN_ICE,     SOUND_TERRAIN_STONE,  SOUND_TERRAIN_ICE }, // TERRAIN_WATER
    { SOUND_TERRAIN_STONE,   SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE,
      SOUND_TERRAIN_STONE,   SOUND_TERRAIN_ICE,    SOUND_TERRAIN_ICE }, // TERRAIN_SLIDE
};
// clang-format on

/**
 * Computes a value that should be added to terrain sounds before playing them.
 * This depends on surfaces and terrain.
 */
u32 mario_get_terrain_sound_addend(struct MarioState *m) {
    s16 floorSoundType;
    s16 terrainType = m->area->terrainType & TERRAIN_MASK;
    s32 ret = SOUND_TERRAIN_DEFAULT << 16;
    s32 floorType;

    if (m->floor) {
        floorType = m->floor->type;

        if ((gCurrLevelNum != LEVEL_LLL) && (m->floorHeight < (m->waterLevel - 10))) {
            // Water terrain sound, excluding LLL since it uses water in the volcano.
            ret = SOUND_TERRAIN_WATER << 16;
        } else if (SURFACE_IS_QUICKSAND(floorType)) {
            ret = SOUND_TERRAIN_SAND << 16;
        } else {
            switch (floorType) {
                default:
                    floorSoundType = 0;
                    break;

                case SURFACE_NOT_SLIPPERY:
                case SURFACE_HARD:
                case SURFACE_HARD_NOT_SLIPPERY:
                case SURFACE_SWITCH:
                    floorSoundType = 1;
                    break;

                case SURFACE_SLIPPERY:
                case SURFACE_HARD_SLIPPERY:
                case SURFACE_NO_CAM_COL_SLIPPERY:
                    floorSoundType = 2;
                    break;

                case SURFACE_VERY_SLIPPERY:
                case SURFACE_ICE:
                case SURFACE_HARD_VERY_SLIPPERY:
                case SURFACE_NOISE_VERY_SLIPPERY_73:
                case SURFACE_NOISE_VERY_SLIPPERY_74:
                case SURFACE_NOISE_VERY_SLIPPERY:
                case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
                    floorSoundType = 3;
                    break;

                case SURFACE_NOISE_DEFAULT:
                    floorSoundType = 4;
                    break;

                case SURFACE_NOISE_SLIPPERY:
                    floorSoundType = 5;
                    break;
            }

            ret = sTerrainSounds[terrainType][floorSoundType] << 16;
        }
    }

    return ret;
}

/**
 * Collides with walls and returns the most recent wall.
 */
struct Surface *resolve_and_return_wall_collisions(Vec3f pos, f32 offset, f32 radius) {
    struct WallCollisionData collisionData;
    struct Surface *wall = NULL;

    collisionData.x = pos[0];
    collisionData.y = pos[1];
    collisionData.z = pos[2];
    collisionData.radius = radius;
    collisionData.offsetY = offset;

    if (find_wall_collisions(&collisionData)) {
        wall = collisionData.walls[collisionData.numWalls - 1];
    }

    pos[0] = collisionData.x;
    pos[1] = collisionData.y;
    pos[2] = collisionData.z;

    // This only returns the most recent wall and can also return NULL
    // there are no wall collisions.
    return wall;
}

/**
 * Finds the ceiling from a vec3f horizontally and a height (with 80 vertical buffer).
 */
f32 vec3f_find_ceil(Vec3f pos, f32 height, struct Surface **ceil) {
    UNUSED f32 unused;

    return find_ceil(pos[0], height + 80.0f, pos[2], ceil);
}

/**
 * Determines if Mario is facing "downhill."
 */
s32 mario_facing_downhill(struct MarioState *m, s32 turnYaw) {
    s16 faceAngleYaw = m->faceAngle[1];

    // This is never used in practice, as turnYaw is
    // always passed as zero.
    if (turnYaw && m->forwardVel < 0.0f) {
        faceAngleYaw += 0x8000;
    }

    faceAngleYaw = m->floorAngle - faceAngleYaw;

    return (-0x4000 < faceAngleYaw) && (faceAngleYaw < 0x4000);
}

/**
 * Determines if a surface is slippery based on the surface class.
 */
u32 mario_floor_is_slippery(struct MarioState *m) {
    f32 normY;

    if ((m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE
        && m->floor->normal.y < 0.9998477f //~cos(1 deg)
    ) {
        return TRUE;
    }

    switch (mario_get_floor_class(m)) {
        case SURFACE_VERY_SLIPPERY:
            normY = 0.9848077f; //~cos(10 deg)
            break;

        case SURFACE_SLIPPERY:
            normY = 0.9396926f; //~cos(20 deg)
            break;

        default:
            normY = 0.7880108f; //~cos(38 deg)
            break;

        case SURFACE_NOT_SLIPPERY:
            normY = 0.0f;
            break;
    }

    return m->floor->normal.y <= normY;
}

/**
 * Determines if a surface is a slope based on the surface class.
 */
s32 mario_floor_is_slope(struct MarioState *m) {
    f32 normY;

    if ((m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE
        && m->floor->normal.y < 0.9998477f //~cos(1 deg)
    ) {
        return TRUE;
    }

    switch (mario_get_floor_class(m)) {
        case SURFACE_VERY_SLIPPERY:
            normY = 0.9961947f; //~cos(5 deg)
            break;

        case SURFACE_SLIPPERY:
            normY = 0.9848077f; //~cos(10 deg)
            break;

        default:
            normY = 0.9659258f; //~cos(15 deg)
            break;

        case SURFACE_NOT_SLIPPERY:
            normY = 0.9396926f; //~cos(20 deg)
            break;
    }

    return m->floor->normal.y <= normY;
}

/**
 * Determines if a surface is steep based on the surface class.
 */
s32 mario_floor_is_steep(struct MarioState *m) {
    f32 normY;
    s32 result = FALSE;

    // Interestingly, this function does not check for the
    // slide terrain type. This means that steep behavior persists for
    // non-slippery and slippery surfaces.
    // This does not matter in vanilla game practice.
    if (!mario_facing_downhill(m, FALSE)) {
        switch (mario_get_floor_class(m)) {
            case SURFACE_VERY_SLIPPERY:
                normY = 0.9659258f; //~cos(15 deg)
                break;

            case SURFACE_SLIPPERY:
                normY = 0.9396926f; //~cos(20 deg)
                break;

            default:
                normY = 0.8660254f; //~cos(30 deg)
                break;

            case SURFACE_NOT_SLIPPERY:
                normY = 0.8660254f; //~cos(30 deg)
                break;
        }

        result = m->floor->normal.y <= normY;
    }

    return result;
}

#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD) && defined(DEBUG)
#define HYRULE_GATE_DIAG_INTERVAL 300
#define HYRULE_GATE_TRIGGER_RADIUS 700.0f
#define HYRULE_GATE_VERTICAL_TOLERANCE 500.0f

static HyruleGateProximityState sHyruleGateProximity;
static u32 sHyruleGateDiagnosticFrames;

/* The selected imported start position is a useful gate-area proximity
 * anchor. The scene does not define a destination transition, so keep the
 * interaction observable and refuse to enqueue an invalid warp.
 */
static void mario_ios_update_hyrule_gate_debug(const struct MarioState *m) {
    HyruleGateProximityEvent event;
    bool nearGate;
    const bool gateDestinationConfigured = false;

    if (m == NULL) return;
    if (gCurrLevelNum != LEVEL_HYRULE_FIELD || gCurrentArea == NULL || gCurrentArea->index != 1) {
        sHyruleGateProximity.inside = false;
        sHyruleGateDiagnosticFrames = 0;
        return;
    }

    nearGate = hyrule_gate_contains(m->pos[0], m->pos[1], m->pos[2],
                                   HYRULE_FIELD_GATE_X, HYRULE_FIELD_GATE_Y,
                                   HYRULE_FIELD_GATE_Z, HYRULE_GATE_TRIGGER_RADIUS,
                                   HYRULE_GATE_VERTICAL_TOLERANCE);
    event = hyrule_gate_update_proximity(&sHyruleGateProximity, nearGate);
    if (event == HYRULE_GATE_ENTERED) {
        const HyruleGateResponse response = hyrule_gate_response(
            true, gateDestinationConfigured, LEVEL_NONE, 0, 0);
        gfx_ios_diag_log("Hyrule gate activated level=%d area=%d mario=(%.1f,%.1f,%.1f) floor=%s floorType=%d floorHeight=%.1f action=0x%08x trigger=proximity destinationConfigured=%d response=%d warpIssued=0",
            gCurrLevelNum, gCurrentArea->index, m->pos[0], m->pos[1], m->pos[2],
            m->floor ? "hit" : "miss", m->floor ? m->floor->type : -1,
            m->floorHeight, m->action, gateDestinationConfigured, response);
        if (response == HYRULE_GATE_RESPONSE_DESTINATION_UNAVAILABLE) {
            gfx_ios_portal_status("Gate destination is not configured");
        }
    } else if (event == HYRULE_GATE_LEFT) {
        gfx_ios_diag_log("Hyrule gate proximity left level=%d area=%d mario=(%.1f,%.1f,%.1f)",
            gCurrLevelNum, gCurrentArea->index, m->pos[0], m->pos[1], m->pos[2]);
    }

    if ((sHyruleGateDiagnosticFrames++ % HYRULE_GATE_DIAG_INTERVAL) == 0) {
        const float dx = m->pos[0] - HYRULE_FIELD_GATE_X;
        const float dz = m->pos[2] - HYRULE_FIELD_GATE_Z;
        gfx_ios_diag_log("Hyrule gate probe level=%d area=%d mario=(%.1f,%.1f,%.1f) gate=(%d,%d,%d) distanceSquared=%.1f near=%d floor=%s floorType=%d interaction=approach-proximity destinationConfigured=%d warpIssued=0 action=0x%08x",
            gCurrLevelNum, gCurrentArea->index, m->pos[0], m->pos[1], m->pos[2],
            HYRULE_FIELD_GATE_X, HYRULE_FIELD_GATE_Y, HYRULE_FIELD_GATE_Z,
            dx * dx + dz * dz, nearGate, m->floor ? "hit" : "miss",
            m->floor ? m->floor->type : -1, gateDestinationConfigured, m->action);
    }
}
#endif

/**
 * Finds the floor height relative from Mario given polar displacement.
 */
f32 find_floor_height_relative_polar(struct MarioState *m, s16 angleFromMario, f32 distFromMario) {
    struct Surface *floor;
    f32 floorY;

    f32 y = sins(m->faceAngle[1] + angleFromMario) * distFromMario;
    f32 x = coss(m->faceAngle[1] + angleFromMario) * distFromMario;

    floorY = find_floor(m->pos[0] + y, m->pos[1] + 100.0f, m->pos[2] + x, &floor);

    return floorY;
}

/**
 * Returns the slope of the floor based off points around Mario.
 */
s16 find_floor_slope(struct MarioState *m, s16 yawOffset) {
    struct Surface *floor;
    f32 forwardFloorY, backwardFloorY;
    f32 forwardYDelta, backwardYDelta;
    s16 result;

    f32 x = sins(m->faceAngle[1] + yawOffset) * 5.0f;
    f32 z = coss(m->faceAngle[1] + yawOffset) * 5.0f;

    forwardFloorY = find_floor(m->pos[0] + x, m->pos[1] + 100.0f, m->pos[2] + z, &floor);
    backwardFloorY = find_floor(m->pos[0] - x, m->pos[1] + 100.0f, m->pos[2] - z, &floor);

    //! If Mario is near OOB, these floorY's can sometimes be -11000.
    //  This will cause these to be off and give improper slopes.
    forwardYDelta = forwardFloorY - m->pos[1];
    backwardYDelta = m->pos[1] - backwardFloorY;

    if (forwardYDelta * forwardYDelta < backwardYDelta * backwardYDelta) {
        result = atan2s(5.0f, forwardYDelta);
    } else {
        result = atan2s(5.0f, backwardYDelta);
    }

    return result;
}

/**
 * Adjusts Mario's camera and sound based on his action status.
 */
void update_mario_sound_and_camera(struct MarioState *m) {
    u32 action = m->action;
    s32 camPreset = m->area->camera->mode;

    if (action == ACT_FIRST_PERSON) {
        raise_background_noise(2);
        gCameraMovementFlags &= ~CAM_MOVE_C_UP_MODE;
        // Go back to the last camera mode
        set_camera_mode(m->area->camera, -1, 1);
    } else if (action == ACT_SLEEPING) {
        raise_background_noise(2);
    }

    if (!(action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER))) {
        if (camPreset == CAMERA_MODE_BEHIND_MARIO || camPreset == CAMERA_MODE_WATER_SURFACE) {
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
        }
    }
}

/**
 * Transitions Mario to a steep jump action.
 */
void set_steep_jump_action(struct MarioState *m) {
    m->marioObj->oMarioSteepJumpYaw = m->faceAngle[1];

    if (m->forwardVel > 0.0f) {
        //! ((s16)0x8000) has undefined behavior. Therefore, this downcast has
        // undefined behavior if m->floorAngle >= 0.
        s16 angleTemp = m->floorAngle + 0x8000;
        s16 faceAngleTemp = m->faceAngle[1] - angleTemp;

        f32 y = sins(faceAngleTemp) * m->forwardVel;
        f32 x = coss(faceAngleTemp) * m->forwardVel * 0.75f;

        m->forwardVel = sqrtf(y * y + x * x);
        m->faceAngle[1] = atan2s(x, y) + angleTemp;
    }

    drop_and_set_mario_action(m, ACT_STEEP_JUMP, 0);
}

/**
 * Sets Mario's vertical speed from his forward speed.
 */
static void set_mario_y_vel_based_on_fspeed(struct MarioState *m, f32 initialVelY, f32 multiplier) {
    // get_additive_y_vel_for_jumps is always 0 and a stubbed function.
    // It was likely trampoline related based on code location.
    m->vel[1] = initialVelY + get_additive_y_vel_for_jumps() + m->forwardVel * multiplier;

    if (m->squishTimer != 0 || m->quicksandDepth > 1.0f) {
        m->vel[1] *= 0.5f;
    }
    m->vel[1] *= ios_reality_global_policy()->jump_multiplier;
}

/**
 * Transitions for a variety of airborne actions.
 */
static u32 set_mario_action_airborne(struct MarioState *m, u32 action, u32 actionArg) {
    f32 fowardVel;

    if (m->squishTimer != 0 || m->quicksandDepth >= 1.0f) {
        if (action == ACT_DOUBLE_JUMP || action == ACT_TWIRLING) {
            action = ACT_JUMP;
        }
    }

    switch (action) {
        case ACT_DOUBLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 52.0f, 0.25f);
            m->forwardVel *= 0.8f;
            break;

        case ACT_BACKFLIP:
            m->marioObj->header.gfx.unk38.animID = -1;
            m->forwardVel = -16.0f;
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            break;

        case ACT_TRIPLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 69.0f, 0.0f);
            m->forwardVel *= 0.8f;
            break;

        case ACT_FLYING_TRIPLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 82.0f, 0.0f);
            break;

        case ACT_WATER_JUMP:
        case ACT_HOLD_WATER_JUMP:
            if (actionArg == 0) {
                set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.0f);
            }
            break;

        case ACT_BURNING_JUMP:
            m->vel[1] = 31.5f;
            m->forwardVel = 8.0f;
            break;

        case ACT_RIDING_SHELL_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            break;

        case ACT_RIDING_YOSHI_AIR:
            // actionArg 1 is used for a walk-off fall; 0 is a mounted jump.
            if (actionArg == 0) {
                set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            }
            break;

        case ACT_IOS_KART_AIR:
            if (actionArg == 0) {
                set_mario_y_vel_based_on_fspeed(m, 32.0f, 0.0f);
            }
            break;

        case ACT_JUMP:
        case ACT_HOLD_JUMP:
            m->marioObj->header.gfx.unk38.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            m->forwardVel *= 0.8f;
            break;

        case ACT_WALL_KICK_AIR:
        case ACT_TOP_OF_POLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            if (m->forwardVel < 24.0f) {
                m->forwardVel = 24.0f;
            }
            m->wallKickTimer = 0;
            break;

        case ACT_SIDE_FLIP:
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            m->forwardVel = 8.0f;
            m->faceAngle[1] = m->intendedYaw;
            break;

        case ACT_STEEP_JUMP:
            m->marioObj->header.gfx.unk38.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            m->faceAngle[0] = -0x2000;
            break;

        case ACT_LAVA_BOOST:
            m->vel[1] = 84.0f;
            if (actionArg == 0) {
                m->forwardVel = 0.0f;
            }
            break;

        case ACT_DIVE:
            if ((fowardVel = m->forwardVel + 15.0f) > 48.0f) {
                fowardVel = 48.0f;
            }
            mario_set_forward_vel(m, fowardVel);
            break;

        case ACT_LONG_JUMP:
            m->marioObj->header.gfx.unk38.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 30.0f, 0.0f);
            m->marioObj->oMarioLongJumpIsSlow = m->forwardVel > 16.0f ? FALSE : TRUE;

            //! (BLJ's) This properly handles long jumps from getting forward speed with
            //  too much velocity, but misses backwards longs allowing high negative speeds.
            if ((m->forwardVel *= 1.5f) > 48.0f) {
                m->forwardVel = 48.0f;
            }
            break;

        case ACT_SLIDE_KICK:
            m->vel[1] = 12.0f;
            if (m->forwardVel < 32.0f) {
                m->forwardVel = 32.0f;
            }
            break;

        case ACT_JUMP_KICK:
            m->vel[1] = 20.0f;
            break;
    }

    m->peakHeight = m->pos[1];
    m->flags |= MARIO_UNKNOWN_08;

    return action;
}

/**
 * Transitions for a variety of moving actions.
 */
static u32 set_mario_action_moving(struct MarioState *m, u32 action, UNUSED u32 actionArg) {
    s16 floorClass = mario_get_floor_class(m);
    f32 forwardVel = m->forwardVel;
    f32 mag = min(m->intendedMag, 8.0f);

    switch (action) {
        case ACT_WALKING:
            if (floorClass != SURFACE_CLASS_VERY_SLIPPERY) {
                if (0.0f <= forwardVel && forwardVel < mag) {
                    m->forwardVel = mag;
                }
            }

            m->marioObj->oMarioWalkingPitch = 0;
            break;

        case ACT_HOLD_WALKING:
            if (0.0f <= forwardVel && forwardVel < mag / 2.0f) {
                m->forwardVel = mag / 2.0f;
            }
            break;

        case ACT_BEGIN_SLIDING:
            if (mario_facing_downhill(m, FALSE)) {
                action = ACT_BUTT_SLIDE;
            } else {
                action = ACT_STOMACH_SLIDE;
            }
            break;

        case ACT_HOLD_BEGIN_SLIDING:
            if (mario_facing_downhill(m, FALSE)) {
                action = ACT_HOLD_BUTT_SLIDE;
            } else {
                action = ACT_HOLD_STOMACH_SLIDE;
            }
            break;
    }

    return action;
}

/**
 * Transition for certain submerged actions, which is actually just the metal jump actions.
 */
static u32 set_mario_action_submerged(struct MarioState *m, u32 action, UNUSED u32 actionArg) {
    if (action == ACT_METAL_WATER_JUMP || action == ACT_HOLD_METAL_WATER_JUMP) {
        m->vel[1] = 32.0f;
    }

    return action;
}

/**
 * Transitions for a variety of cutscene actions.
 */
static u32 set_mario_action_cutscene(struct MarioState *m, u32 action, UNUSED u32 actionArg) {
    switch (action) {
        case ACT_EMERGE_FROM_PIPE:
            m->vel[1] = 52.0f;
            break;

        case ACT_FALL_AFTER_STAR_GRAB:
            mario_set_forward_vel(m, 0.0f);
            break;

        case ACT_SPAWN_SPIN_AIRBORNE:
            mario_set_forward_vel(m, 2.0f);
            break;

        case ACT_SPECIAL_EXIT_AIRBORNE:
        case ACT_SPECIAL_DEATH_EXIT:
            m->vel[1] = 64.0f;
            break;
    }

    return action;
}

/**
 * Puts Mario into a given action, putting Mario through the appropriate
 * specific function if needed.
 */
u32 set_mario_action(struct MarioState *m, u32 action, u32 actionArg) {
    switch (action & ACT_GROUP_MASK) {
        case ACT_GROUP_MOVING:
            action = set_mario_action_moving(m, action, actionArg);
            break;

        case ACT_GROUP_AIRBORNE:
            action = set_mario_action_airborne(m, action, actionArg);
            break;

        case ACT_GROUP_SUBMERGED:
            action = set_mario_action_submerged(m, action, actionArg);
            break;

        case ACT_GROUP_CUTSCENE:
            action = set_mario_action_cutscene(m, action, actionArg);
            break;
    }

    // Resets the sound played flags, meaning Mario can play those sound types again.
    m->flags &= ~(MARIO_ACTION_SOUND_PLAYED | MARIO_MARIO_SOUND_PLAYED);

    if (!(m->action & ACT_FLAG_AIR)) {
        m->flags &= ~MARIO_UNKNOWN_18;
    }

    // Initialize the action information.
    m->prevAction = m->action;
    m->action = action;
    m->actionArg = actionArg;
    m->actionState = 0;
    m->actionTimer = 0;

    return TRUE;
}

/**
 * Puts Mario into a specific jumping action from a landing action.
 */
s32 set_jump_from_landing(struct MarioState *m) {
    if (m->quicksandDepth >= 11.0f) {
        if (m->heldObj == NULL) {
            return set_mario_action(m, ACT_QUICKSAND_JUMP_LAND, 0);
        } else {
            return set_mario_action(m, ACT_HOLD_QUICKSAND_JUMP_LAND, 0);
        }
    }

    if (mario_floor_is_steep(m)) {
        set_steep_jump_action(m);
    } else {
        if ((m->doubleJumpTimer == 0) || (m->squishTimer != 0)) {
            set_mario_action(m, ACT_JUMP, 0);
        } else {
            switch (m->prevAction) {
                case ACT_JUMP_LAND:
                    set_mario_action(m, ACT_DOUBLE_JUMP, 0);
                    break;

                case ACT_FREEFALL_LAND:
                    set_mario_action(m, ACT_DOUBLE_JUMP, 0);
                    break;

                case ACT_SIDE_FLIP_LAND_STOP:
                    set_mario_action(m, ACT_DOUBLE_JUMP, 0);
                    break;

                case ACT_DOUBLE_JUMP_LAND:
                    // If Mario has a wing cap, he ignores the typical speed
                    // requirement for a triple jump.
                    if (m->flags & MARIO_WING_CAP) {
                        set_mario_action(m, ACT_FLYING_TRIPLE_JUMP, 0);
                    } else if (m->forwardVel > 20.0f) {
                        set_mario_action(m, ACT_TRIPLE_JUMP, 0);
                    } else {
                        set_mario_action(m, ACT_JUMP, 0);
                    }
                    break;

                default:
                    set_mario_action(m, ACT_JUMP, 0);
                    break;
            }
        }
    }

    m->doubleJumpTimer = 0;

    return TRUE;
}

/**
 * Puts Mario in a given action, as long as it is not overruled by
 * either a quicksand or steep jump.
 */
s32 set_jumping_action(struct MarioState *m, u32 action, u32 actionArg) {
    UNUSED u32 currAction = m->action;

    if (m->quicksandDepth >= 11.0f) {
        // Checks whether Mario is holding an object or not.
        if (m->heldObj == NULL) {
            return set_mario_action(m, ACT_QUICKSAND_JUMP_LAND, 0);
        } else {
            return set_mario_action(m, ACT_HOLD_QUICKSAND_JUMP_LAND, 0);
        }
    }

    if (mario_floor_is_steep(m)) {
        set_steep_jump_action(m);
    } else {
        set_mario_action(m, action, actionArg);
    }

    return TRUE;
}

/**
 * Drop anything Mario is holding and set a new action.
 */
s32 drop_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg) {
    mario_stop_riding_and_holding(m);

    return set_mario_action(m, action, actionArg);
}

/**
 * Increment Mario's hurt counter and set a new action.
 */
s32 hurt_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg, s16 hurtCounter) {
    m->hurtCounter = hurtCounter;

    return set_mario_action(m, action, actionArg);
}

/**
 * Checks a variety of inputs for common transitions between many different
 * actions. A common variant of the below function.
 */
s32 check_common_action_exits(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }

    return FALSE;
}

/**
 * Checks a variety of inputs for common transitions between many different
 * object holding actions. A holding variant of the above function.
 */
s32 check_common_hold_action_exits(struct MarioState *m) {
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_HOLD_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_HOLD_FREEFALL, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_HOLD_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_HOLD_BEGIN_SLIDING, 0);
    }

    return FALSE;
}

/**
 * Transitions Mario from a submerged action to a walking action.
 */
s32 transition_submerged_to_walking(struct MarioState *m) {
    set_camera_mode(m->area->camera, m->area->camera->defMode, 1);

    vec3s_set(m->angleVel, 0, 0, 0);

    if (m->heldObj == NULL) {
        return set_mario_action(m, ACT_WALKING, 0);
    } else {
        return set_mario_action(m, ACT_HOLD_WALKING, 0);
    }
}

/**
 * This is the transition function typically for entering a submerged action for a
 * non-submerged action. This also applies the water surface camera preset.
 */
s32 set_water_plunge_action(struct MarioState *m) {
    m->forwardVel = m->forwardVel / 4.0f;
    m->vel[1] = m->vel[1] / 2.0f;

    m->pos[1] = m->waterLevel - 100;

    m->faceAngle[2] = 0;

    vec3s_set(m->angleVel, 0, 0, 0);

    if ((m->action & ACT_FLAG_DIVING) == 0) {
        m->faceAngle[0] = 0;
    }

    if (m->area->camera->mode != CAMERA_MODE_WATER_SURFACE) {
        set_camera_mode(m->area->camera, CAMERA_MODE_WATER_SURFACE, 1);
    }

    return set_mario_action(m, ACT_WATER_PLUNGE, 0);
}

/**
 * These are the scaling values for the x and z axis for Mario
 * when he is close to unsquishing.
 */
u8 sSquishScaleOverTime[16] = { 0x46, 0x32, 0x32, 0x3C, 0x46, 0x50, 0x50, 0x3C,
                                0x28, 0x14, 0x14, 0x1E, 0x32, 0x3C, 0x3C, 0x28 };

/**
 * Applies the squish to Mario's model via scaling.
 */
void squish_mario_model(struct MarioState *m) {
    if (m->squishTimer != 0xFF) {
        // If no longer squished, scale back to default.
        // Also handles the Tiny Mario and Huge Mario cheats.
        if (m->squishTimer == 0) {
            if (Cheats.EnableCheats) {
                if (Cheats.HugeMario) {
                    vec3f_set(m->marioObj->header.gfx.scale, 2.5f, 2.5f, 2.5f);
                }
                else if (Cheats.TinyMario) {
                    vec3f_set(m->marioObj->header.gfx.scale, 0.2f, 0.2f, 0.2f);
                }
                else {
                    vec3f_set(m->marioObj->header.gfx.scale, 1.0f, 1.0f, 1.0f);
                }
            }
            else {
                vec3f_set(m->marioObj->header.gfx.scale, 1.0f, 1.0f, 1.0f);
            }
            
        }
        // If timer is less than 16, rubber-band Mario's size scale up and down.
        else if (m->squishTimer <= 16) {
            m->squishTimer -= 1;

            m->marioObj->header.gfx.scale[1] =
                1.0f - ((sSquishScaleOverTime[15 - m->squishTimer] * 0.6f) / 100.0f);
            m->marioObj->header.gfx.scale[0] =
                ((sSquishScaleOverTime[15 - m->squishTimer] * 0.4f) / 100.0f) + 1.0f;

            m->marioObj->header.gfx.scale[2] = m->marioObj->header.gfx.scale[0];
        } else {
            m->squishTimer -= 1;

            vec3f_set(m->marioObj->header.gfx.scale, 1.4f, 0.4f, 1.4f);
        }
    }
}

/**
 * Debug function that prints floor normal, velocity, and action information.
 */
void debug_print_speed_action_normal(struct MarioState *m) {
    f32 steepness;
    f32 floor_nY;

    if (gShowDebugText) {
        steepness = sqrtf(
            ((m->floor->normal.x * m->floor->normal.x) + (m->floor->normal.z * m->floor->normal.z)));
        floor_nY = m->floor->normal.y;

        print_text_fmt_int(210, 88, "ANG %d", (atan2s(floor_nY, steepness) * 180.0f) / 32768.0f);

        print_text_fmt_int(210, 72, "SPD %d", m->forwardVel);

        // STA short for "status," the official action name via SMS map.
        print_text_fmt_int(210, 56, "STA %x", (m->action & ACT_ID_MASK));
    }
}

/**
 * Update the button inputs for Mario.
 */
void update_mario_button_inputs(struct MarioState *m) {
    if (m->controller->buttonPressed & A_BUTTON) {
        m->input |= INPUT_A_PRESSED;
    }

    if (m->controller->buttonDown & A_BUTTON) {
        m->input |= INPUT_A_DOWN;
    }

    // Don't update for these buttons if squished.
    if (m->squishTimer == 0) {
        if (m->controller->buttonPressed & B_BUTTON) {
            m->input |= INPUT_B_PRESSED;
        }

        if (m->controller->buttonDown & Z_TRIG) {
            m->input |= INPUT_Z_DOWN;
        }

        if (m->controller->buttonPressed & Z_TRIG) {
            m->input |= INPUT_Z_PRESSED;
        }
    }

    if (m->input & INPUT_A_PRESSED) {
        m->framesSinceA = 0;
    } else if (m->framesSinceA < 0xFF) {
        m->framesSinceA += 1;
    }

    if (m->input & INPUT_B_PRESSED) {
        m->framesSinceB = 0;
    } else if (m->framesSinceB < 0xff) {
        m->framesSinceB += 1;
    }
}

/**
 * Updates the joystick intended magnitude.
 */
void update_mario_joystick_inputs(struct MarioState *m) {
    struct Controller *controller = m->controller;
    f32 mag = ((controller->stickMag / 64.0f) * (controller->stickMag / 64.0f)) * 64.0f;

    if (m->squishTimer == 0) {
        m->intendedMag = mag / 2.0f;
    } else {
        m->intendedMag = mag / 8.0f;
    }

    if (m->intendedMag > 0.0f) {
#ifndef BETTERCAMERA
        m->intendedYaw = atan2s(-controller->stickY, controller->stickX) + m->area->camera->yaw;
#else
        if (gLakituState.mode != CAMERA_MODE_NEWCAM)
            m->intendedYaw = atan2s(-controller->stickY, controller->stickX) + m->area->camera->yaw;
        else
            m->intendedYaw = atan2s(-controller->stickY, controller->stickX)-newcam_yaw+0x4000;
#endif
        m->input |= INPUT_NONZERO_ANALOG;
    } else {
        m->intendedYaw = m->faceAngle[1];
    }
}

/**
 * Resolves wall collisions, and updates a variety of inputs.
 */
void update_mario_geometry_inputs(struct MarioState *m) {
    f32 gasLevel;
    f32 ceilToFloorDist;

    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 60.0f, 50.0f);
    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 30.0f, 24.0f);

    m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);

    // If Mario is OOB, move his position to his graphical position (which was not updated)
    // and check for the floor there.
    // This can cause errant behavior when combined with astral projection,
    // since the graphical position was not Mario's previous location.
    if (m->floor == NULL) {
        vec3f_copy(m->pos, m->marioObj->header.gfx.pos);
        m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);
    }

    m->ceilHeight = vec3f_find_ceil(&m->pos[0], m->floorHeight, &m->ceil);
    gasLevel = find_poison_gas_level(m->pos[0], m->pos[2]);
    m->waterLevel = find_water_level(m->pos[0], m->pos[2]);
    if (ios_reality_global_policy()->swim_anywhere
        && !mario_ios_is_driving_kart(m) && !mario_ios_is_riding_yoshi(m)
        && m->waterLevel < m->pos[1] + 64.0f) {
        m->waterLevel = m->pos[1] + 64.0f;
    }

    if (m->floor) {
        m->floorAngle = atan2s(m->floor->normal.z, m->floor->normal.x);
        m->terrainSoundAddend = mario_get_terrain_sound_addend(m);

        if ((m->pos[1] > m->waterLevel - 40) && mario_floor_is_slippery(m)) {
            m->input |= INPUT_ABOVE_SLIDE;
        }

        if ((m->floor->flags & SURFACE_FLAG_DYNAMIC)
            || (m->ceil && m->ceil->flags & SURFACE_FLAG_DYNAMIC)) {
            ceilToFloorDist = m->ceilHeight - m->floorHeight;

            if ((0.0f <= ceilToFloorDist) && (ceilToFloorDist <= 150.0f)) {
                m->input |= INPUT_SQUISHED;
            }
        }

        if (m->pos[1] > m->floorHeight + 100.0f) {
            m->input |= INPUT_OFF_FLOOR;
        }

        if (m->pos[1] < (m->waterLevel - 10)) {
            m->input |= INPUT_IN_WATER;
        }

        if (m->pos[1] < (gasLevel - 100.0f)) {
            m->input |= INPUT_IN_POISON_GAS;
        }

    } else {
        level_trigger_warp(m, WARP_OP_DEATH);
    }
}

/**
 * Handles Mario's input flags as well as a couple timers.
 */
void update_mario_inputs(struct MarioState *m) {
    m->particleFlags = 0;
    m->input = 0;
    m->collidedObjInteractTypes = m->marioObj->collidedObjInteractTypes;
    m->flags &= 0xFFFFFF;

    update_mario_button_inputs(m);
    update_mario_joystick_inputs(m);
    update_mario_geometry_inputs(m);

    debug_print_speed_action_normal(m);
    
    /* Moonjump cheat */
    while (Cheats.MoonJump == true && Cheats.EnableCheats == true && m->controller->buttonDown & L_TRIG ){
        m->vel[1] = 25;
        break;   // TODO: Unneeded break?
    }
    /*End of moonjump cheat */

    if (gCameraMovementFlags & CAM_MOVE_C_UP_MODE) {
        if (m->action & ACT_FLAG_ALLOW_FIRST_PERSON) {
            m->input |= INPUT_FIRST_PERSON;
        } else {
            gCameraMovementFlags &= ~CAM_MOVE_C_UP_MODE;
        }
    }

    if (!(m->input & (INPUT_NONZERO_ANALOG | INPUT_A_PRESSED))) {
        m->input |= INPUT_UNKNOWN_5;
    }

    if (m->marioObj->oInteractStatus
        & (INT_STATUS_HOOT_GRABBED_BY_MARIO | INT_STATUS_MARIO_UNK1 | INT_STATUS_MARIO_UNK4)) {
        m->input |= INPUT_UNKNOWN_10;
    }

    // This function is located near other unused trampoline functions,
    // perhaps logically grouped here with the timers.
    stub_mario_step_1(m);

    if (m->wallKickTimer > 0) {
        m->wallKickTimer--;
    }

    if (m->doubleJumpTimer > 0) {
        m->doubleJumpTimer--;
    }
}

/**
 * Set's the camera preset for submerged action behaviors.
 */
void set_submerged_cam_preset_and_spawn_bubbles(struct MarioState *m) {
    f32 heightBelowWater;
    s16 camPreset;

    if ((m->action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED) {
        heightBelowWater = (f32)(m->waterLevel - 80) - m->pos[1];
        camPreset = m->area->camera->mode;

        if ((m->action & ACT_FLAG_METAL_WATER)) {
            if (camPreset != CAMERA_MODE_CLOSE) {
                set_camera_mode(m->area->camera, CAMERA_MODE_CLOSE, 1);
            }
        } else {
            if ((heightBelowWater > 800.0f) && (camPreset != CAMERA_MODE_BEHIND_MARIO)) {
                set_camera_mode(m->area->camera, CAMERA_MODE_BEHIND_MARIO, 1);
            }

            if ((heightBelowWater < 400.0f) && (camPreset != CAMERA_MODE_WATER_SURFACE)) {
                set_camera_mode(m->area->camera, CAMERA_MODE_WATER_SURFACE, 1);
            }

            // As long as Mario isn't drowning or at the top
            // of the water with his head out, spawn bubbles.
            if ((m->action & ACT_FLAG_INTANGIBLE) == 0) {
                if ((m->pos[1] < (f32)(m->waterLevel - 160)) || (m->faceAngle[0] < -0x800)) {
                    m->particleFlags |= PARTICLE_BUBBLE;
                }
            }
        }
    }
}

/**
 * Both increments and decrements Mario's HP.
 */
void update_mario_health(struct MarioState *m) {
    s32 terrainIsSnow;

    if (m->health >= 0x100) {
        // When already healing or hurting Mario, Mario's HP is not changed any more here.
        if (((u32) m->healCounter | (u32) m->hurtCounter) == 0) {
            if ((m->input & INPUT_IN_POISON_GAS) && ((m->action & ACT_FLAG_INTANGIBLE) == 0)) {
                if (((m->flags & MARIO_METAL_CAP) == 0) && (gDebugLevelSelect == 0)) {
                    m->health -= 4;
                }
            } else {
                if ((m->action & ACT_FLAG_SWIMMING) && ((m->action & ACT_FLAG_INTANGIBLE) == 0)) {
                    terrainIsSnow = (m->area->terrainType & TERRAIN_MASK) == TERRAIN_SNOW;

                    // When Mario is near the water surface, recover health (unless in snow),
                    // when in snow terrains lose 3 health.
                    // If using the debug level select, do not lose any HP to water.
                    if ((m->pos[1] >= (m->waterLevel - 140)) && !terrainIsSnow) {
                        m->health += 0x1A;
                    } else if (gDebugLevelSelect == 0) {
                        m->health -= (terrainIsSnow ? 3 : 1);
                    }
                }
            }
        }

        if (m->healCounter > 0) {
            m->health += 0x40;
            m->healCounter--;
        }
        if (m->hurtCounter > 0) {
            m->health -= 0x40;
            m->hurtCounter--;
        }

        if (m->health >= 0x881) {
            m->health = 0x880;
        }
        if (m->health < 0x100) {
            m->health = 0xFF;
        }

        // Play a noise to alert the player when Mario is close to drowning.
        if (((m->action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED) && (m->health < 0x300)) {
            play_sound(SOUND_MOVING_ALMOST_DROWNING, gDefaultSoundArgs);
            if (!gRumblePakTimer) {
                gRumblePakTimer = 36;
                if (is_rumble_finished_and_queue_empty()) {
                    queue_rumble_data(3, 30);
                }
            }
        } else {
            gRumblePakTimer = 0;
        }
    }
}

/**
 * Updates some basic info for camera usage.
 */
void update_mario_info_for_cam(struct MarioState *m) {
    m->marioBodyState->action = m->action;
    m->statusForCamera->action = m->action;

    vec3s_copy(m->statusForCamera->faceAngle, m->faceAngle);

    if ((m->flags & MARIO_UNKNOWN_25) == 0) {
        vec3f_copy(m->statusForCamera->pos, m->pos);
    }
}

/**
 * Resets Mario's model, done every time an action is executed.
 */
void mario_reset_bodystate(struct MarioState *m) {
    struct MarioBodyState *bodyState = m->marioBodyState;

    bodyState->capState = MARIO_HAS_DEFAULT_CAP_OFF;
    bodyState->eyeState = MARIO_EYES_BLINK;
    bodyState->handState = MARIO_HAND_FISTS;
    bodyState->modelState = 0;
    bodyState->wingFlutter = FALSE;

    m->flags &= ~MARIO_METAL_SHOCK;
}

/**
 * Adjusts Mario's graphical height for quicksand.
 */
void sink_mario_in_quicksand(struct MarioState *m) {
    struct Object *o = m->marioObj;

    if (o->header.gfx.throwMatrix) {
        (*o->header.gfx.throwMatrix)[3][1] -= m->quicksandDepth;
    }

    o->header.gfx.pos[1] -= m->quicksandDepth;
}

/**
 * Is a binary representation of the frames to flicker Mario's cap when the timer
 * is running out.
 *
 * Equals [1000]^5 . [100]^8 . [10]^9 . [1] in binary, which is
 * 100010001000100010001001001001001001001001001010101010101010101.
 */
u64 sCapFlickerFrames = 0x4444449249255555;

/**
 * Updates the cap flags mainly based on the cap timer.
 */
u32 update_and_return_cap_flags(struct MarioState *m) {
    u32 flags = m->flags;
    u32 action;

    if (m->capTimer > 0) {
        action = m->action;

        if ((m->capTimer <= 60)
            || ((action != ACT_READING_AUTOMATIC_DIALOG) && (action != ACT_READING_NPC_DIALOG)
                && (action != ACT_READING_SIGN) && (action != ACT_IN_CANNON))) {
            m->capTimer -= 1;
        }

        if (m->capTimer == 0) {
            stop_cap_music();

            m->flags &= ~(MARIO_VANISH_CAP | MARIO_METAL_CAP | MARIO_WING_CAP);
            if ((m->flags & (MARIO_NORMAL_CAP | MARIO_VANISH_CAP | MARIO_METAL_CAP | MARIO_WING_CAP))
                == 0) {
                m->flags &= ~MARIO_CAP_ON_HEAD;
            }
        }

        if (m->capTimer == 0x3C) {
            fadeout_cap_music();
        }

        // This code flickers the cap through a long binary string, increasing in how
        // common it flickers near the end.
        if ((m->capTimer < 0x40) && ((1ULL << m->capTimer) & sCapFlickerFrames)) {
            flags &= ~(MARIO_VANISH_CAP | MARIO_METAL_CAP | MARIO_WING_CAP);
            if ((flags & (MARIO_NORMAL_CAP | MARIO_VANISH_CAP | MARIO_METAL_CAP | MARIO_WING_CAP))
                == 0) {
                flags &= ~MARIO_CAP_ON_HEAD;
            }
        }
    }

    return flags;
}

/**
 * Updates the Mario's cap, rendering, and hitbox.
 */
void mario_update_hitbox_and_cap_model(struct MarioState *m) {
    struct MarioBodyState *bodyState = m->marioBodyState;
    s32 flags = update_and_return_cap_flags(m);
#ifdef IOS_HAT_MOVEMENT
    if (m == gMarioState && mario_hat_is_away()) {
        flags &= ~MARIO_CAP_ON_HEAD;
    }
#endif

    if (flags & MARIO_VANISH_CAP) {
        bodyState->modelState = MODEL_STATE_NOISE_ALPHA;
    }

    if (flags & MARIO_METAL_CAP) {
        bodyState->modelState |= MODEL_STATE_METAL;
    }

    if (flags & MARIO_METAL_SHOCK) {
        bodyState->modelState |= MODEL_STATE_METAL;
    }

    if (m->invincTimer >= 3) {
        //! (Pause buffered hitstun) Since the global timer increments while paused,
        //  this can be paused through to give continual invisibility. This leads to
        //  no interaction with objects.
        if (gGlobalTimer & 1) {
            gMarioState->marioObj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
        }
    }

    if (flags & MARIO_CAP_IN_HAND) {
        if (flags & MARIO_WING_CAP) {
            bodyState->handState = MARIO_HAND_HOLDING_WING_CAP;
        } else {
            bodyState->handState = MARIO_HAND_HOLDING_CAP;
        }
    }

    if (flags & MARIO_CAP_ON_HEAD) {
        if (flags & MARIO_WING_CAP) {
            bodyState->capState = MARIO_HAS_WING_CAP_ON;
        } else {
            bodyState->capState = MARIO_HAS_DEFAULT_CAP_ON;
        }
    }

    // Short hitbox for crouching/crawling/etc.
    if (m->action & ACT_FLAG_SHORT_HITBOX) {
        m->marioObj->hitboxHeight = 100.0f;
    } else {
        m->marioObj->hitboxHeight = 160.0f;
    }

    if ((m->flags & MARIO_TELEPORTING) && (m->fadeWarpOpacity != 0xFF)) {
        bodyState->modelState &= ~0xFF;
        bodyState->modelState |= (0x100 | m->fadeWarpOpacity);
    }
}

/**
 * An unused and possibly a debug function. Z + another button input
 * sets Mario with a different cap.
 */
static void debug_update_mario_cap(u16 button, s32 flags, u16 capTimer, u16 capMusic) {
    // This checks for Z_TRIG instead of Z_DOWN flag
    // (which is also what other debug functions do),
    // so likely debug behavior rather than unused behavior.
    if ((gPlayer1Controller->buttonDown & Z_TRIG) && (gPlayer1Controller->buttonPressed & button)
        && ((gMarioState->flags & flags) == 0)) {
        gMarioState->flags |= (flags + MARIO_CAP_ON_HEAD);

        if (capTimer > gMarioState->capTimer) {
            gMarioState->capTimer = capTimer;
        }

        play_cap_music(capMusic);
    }
}

void func_sh_8025574C(void) {
    if (gMarioState->particleFlags & PARTICLE_HORIZONTAL_STAR) {
        queue_rumble_data(5, 80);
    } else if (gMarioState->particleFlags & PARTICLE_VERTICAL_STAR) {
        queue_rumble_data(5, 80);
    } else if (gMarioState->particleFlags & PARTICLE_TRIANGLE) {
        queue_rumble_data(5, 80);
    }
    if(gMarioState->heldObj && gMarioState->heldObj->behavior == segmented_to_virtual(bhvBobomb)) {
        reset_rumble_timers();
    }
}

/**
 * Main function for executing Mario's behavior.
 */
s32 execute_mario_action(UNUSED struct Object *o) {
    s32 inLoop = TRUE;
    Vec3f previousPosition;
    /**
    * Cheat stuff
    */

#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD) && defined(DEBUG)
    mario_ios_update_hyrule_gate_debug(gMarioState);
#endif

    if (Cheats.EnableCheats)
    {
        if (Cheats.GodMode)
            gMarioState->health = 0x880;

        if (Cheats.InfiniteLives && gMarioState->numLives < 99)
            gMarioState->numLives += 1;

        if (Cheats.SuperSpeed && gMarioState->forwardVel > 0)
            gMarioState->forwardVel += 100;
    }
    /**
    * End of cheat stuff
    */
    if (gMarioState->action) {
        gMarioState->marioObj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
        mario_reset_bodystate(gMarioState);
        vec3f_copy(previousPosition, gMarioState->pos);
        mario_ios_validate_kart_area(gMarioState);
        if (sIOSPortalTraversalCooldown > 0) sIOSPortalTraversalCooldown--;
        update_mario_inputs(gMarioState);
        {
            const IOSRealityPolicy *policy = ios_reality_global_policy();
            if (policy->movement_multiplier != 1.0f && !mario_ios_is_driving_kart(gMarioState))
                gMarioState->intendedMag *= policy->movement_multiplier;
        }
        mario_handle_special_floors(gMarioState);
        mario_process_interactions(gMarioState);

        // If Mario is OOB, stop executing actions.
        if (gMarioState->floor == NULL) {
            mario_ios_restore_kart_to_last_ground();
            mario_ios_clear_kart_mount(gMarioState);
#ifdef IOS_HAT_MOVEMENT
            mario_hat_reset();
#endif
            return 0;
        }

        ios_reality_process_requests();
        {
            IOSRealityCheatId resultId;
            IOSRealityResult result = ios_reality_global_last_result(&resultId);
            if (result != IOS_REALITY_RESULT_OK
                && ((s32)resultId != sIOSRealityLoggedResultId || (s32)result != sIOSRealityLoggedResult)) {
                const IOSRealityCheatDefinition *definition = ios_reality_definition(resultId);
                const char *reason = result == IOS_REALITY_RESULT_CONFLICT ? "conflicts with an enabled cheat"
                    : result == IOS_REALITY_RESULT_DEPENDENCY ? "required dependency is disabled"
                    : result == IOS_REALITY_RESULT_UNAVAILABLE ? "feature is not implemented on this build"
                    : "request is not a toggleable cheat";
                IOS_REALITY_EVENT("Reality cheat rejected id=%d name=%s reason=%s", (s32)resultId,
                    definition ? definition->name : "unknown", reason);
                IOS_REALITY_STATUS(reason);
            }
            sIOSRealityLoggedResultId = (s32)resultId;
            sIOSRealityLoggedResult = (s32)result;
        }
        mario_ios_apply_gameplay_requests(gMarioState);
        mario_ios_process_reality_actions(gMarioState);
        mario_ios_process_kart_interaction(gMarioState);

        // Damage, death, or another action transition may interrupt the
        // vehicle action. Release the attachment but leave the kart at the
        // collision-resolved position in the level.
        mario_ios_detach_interrupted_kart(gMarioState);

        // A press close to the dedicated test Yoshi mounts instead of jumping.
        // The same action is available from the native Gameplay diagnostics menu.
        if (gMarioState->input & INPUT_A_PRESSED) {
            mario_ios_try_mount_yoshi(gMarioState);
        }

        // The function can loop through many action shifts in one frame,
        // which can lead to unexpected sub-frame behavior. Could potentially hang
        // if a loop of actions were found, but there has not been a situation found.
#ifdef IOS_HAT_MOVEMENT
        mario_hat_update(gMarioState);
#endif
        while (inLoop) {
#ifdef IOS_HAT_MOVEMENT
            if (gMarioState->action == ACT_IOS_HAT_THROW) {
                mario_hat_execute_throw(gMarioState);
            }
#endif
            switch (gMarioState->action & ACT_GROUP_MASK) {
                case ACT_GROUP_STATIONARY:
                    inLoop = mario_execute_stationary_action(gMarioState);
                    break;

                case ACT_GROUP_MOVING:
                    inLoop = mario_execute_moving_action(gMarioState);
                    break;

                case ACT_GROUP_AIRBORNE:
                    inLoop = mario_execute_airborne_action(gMarioState);
                    break;

                case ACT_GROUP_SUBMERGED:
                    inLoop = mario_execute_submerged_action(gMarioState);
                    break;

                case ACT_GROUP_CUTSCENE:
                    inLoop = mario_execute_cutscene_action(gMarioState);
                    break;

                case ACT_GROUP_AUTOMATIC:
                    inLoop = mario_execute_automatic_action(gMarioState);
                    break;

                case ACT_GROUP_OBJECT:
                    inLoop = mario_execute_object_action(gMarioState);
                    break;
            }
        }

        mario_ios_try_portal_traversal(gMarioState, previousPosition);

        // Damage, death, cutscenes, and other normal action transitions release
        // the test mount so the player never keeps a stale object attachment.
        if (gMarioState->riddenObj != NULL
            && gMarioState->riddenObj->behavior == segmented_to_virtual(bhvRideableYoshi)
            && !mario_ios_is_riding_yoshi(gMarioState)) {
            mario_ios_detach_yoshi(gMarioState);
        }
        mario_ios_detach_interrupted_kart(gMarioState);

        sink_mario_in_quicksand(gMarioState);
        squish_mario_model(gMarioState);
        if (gMarioState->squishTimer == 0) {
            f32 scale = ios_reality_global_policy()->mario_scale;
            gMarioState->marioObj->header.gfx.scale[0] *= scale;
            gMarioState->marioObj->header.gfx.scale[1] *= scale;
            gMarioState->marioObj->header.gfx.scale[2] *= scale;
        }
        set_submerged_cam_preset_and_spawn_bubbles(gMarioState);
        update_mario_health(gMarioState);
        update_mario_info_for_cam(gMarioState);
#ifdef IOS_HAT_MOVEMENT
        mario_hat_finish_frame(gMarioState);
#endif
        mario_update_hitbox_and_cap_model(gMarioState);
        if (mario_ios_is_riding_yoshi(gMarioState)) {
            // Keep collision, floor, and camera coordinates at ground level while
            // placing Mario visibly on Yoshi's back.
            gMarioState->marioObj->header.gfx.pos[1] += 82.0f;
        } else if (mario_ios_is_driving_kart(gMarioState)) {
            gMarioState->marioObj->header.gfx.pos[1] += IOS_KART_DRIVER_RENDER_HEIGHT;
        }

        // Both of the wind handling portions play wind audio only in
        // non-Japanese releases.
        if (gMarioState->floor->type == SURFACE_HORIZONTAL_WIND) {
            spawn_wind_particles(0, (gMarioState->floor->force << 8));
#ifndef VERSION_JP
            play_sound(SOUND_ENV_WIND2, gMarioState->marioObj->header.gfx.cameraToObject);
#endif
        }

        if (gMarioState->floor->type == SURFACE_VERTICAL_WIND) {
            spawn_wind_particles(1, 0);
#ifndef VERSION_JP
            play_sound(SOUND_ENV_WIND2, gMarioState->marioObj->header.gfx.cameraToObject);
#endif
        }

        play_infinite_stairs_music();
        gMarioState->marioObj->oInteractStatus = 0;
        func_sh_8025574C();

        return gMarioState->particleFlags;
    }

    return 0;
}

/**************************************************
 *                  INITIALIZATION                *
 **************************************************/

void init_mario(void) {
    Vec3s capPos;
    struct Object *capObject;

    unused80339F10 = 0;

#ifdef IOS_HAT_MOVEMENT
    mario_hat_reset();
#endif

    ios_reality_global_init();

    mario_ios_reset_portals();

    gMarioState->actionTimer = 0;
    mario_set_temporary_luigi_palette(sTemporaryLuigiPaletteEnabled);
    gMarioState->framesSinceA = 0xFF;
    gMarioState->framesSinceB = 0xFF;

    gMarioState->invincTimer = 0;

    if (save_file_get_flags()
        & (SAVE_FLAG_CAP_ON_GROUND | SAVE_FLAG_CAP_ON_KLEPTO | SAVE_FLAG_CAP_ON_UKIKI
           | SAVE_FLAG_CAP_ON_MR_BLIZZARD)) {
        gMarioState->flags = 0;
    } else {
        gMarioState->flags = (MARIO_CAP_ON_HEAD | MARIO_NORMAL_CAP);
    }

    gMarioState->forwardVel = 0.0f;
    gMarioState->squishTimer = 0;

    gMarioState->hurtCounter = 0;
    gMarioState->healCounter = 0;

    gMarioState->capTimer = 0;
    gMarioState->quicksandDepth = 0.0f;

    gMarioState->heldObj = NULL;
    gMarioState->riddenObj = NULL;
    gMarioState->usedObj = NULL;

    gMarioState->waterLevel =
        find_water_level(gMarioSpawnInfo->startPos[0], gMarioSpawnInfo->startPos[2]);

    gMarioState->area = gCurrentArea;
    gMarioState->marioObj = gMarioObject;
    gMarioState->marioObj->header.gfx.unk38.animID = -1;
    vec3s_copy(gMarioState->faceAngle, gMarioSpawnInfo->startAngle);
    vec3s_set(gMarioState->angleVel, 0, 0, 0);
    vec3s_to_vec3f(gMarioState->pos, gMarioSpawnInfo->startPos);
    vec3f_set(gMarioState->vel, 0, 0, 0);
    gMarioState->floorHeight =
        find_floor(gMarioState->pos[0], gMarioState->pos[1], gMarioState->pos[2], &gMarioState->floor);

    if (gMarioState->pos[1] < gMarioState->floorHeight) {
        gMarioState->pos[1] = gMarioState->floorHeight;
    }

    gMarioState->marioObj->header.gfx.pos[1] = gMarioState->pos[1];

    gMarioState->action =
        (gMarioState->pos[1] <= (gMarioState->waterLevel - 100)) ? ACT_WATER_IDLE : ACT_IDLE;

    mario_reset_bodystate(gMarioState);
    update_mario_info_for_cam(gMarioState);
    gMarioState->marioBodyState->punchState = 0;

    gMarioState->marioObj->oPosX = gMarioState->pos[0];
    gMarioState->marioObj->oPosY = gMarioState->pos[1];
    gMarioState->marioObj->oPosZ = gMarioState->pos[2];

    gMarioState->marioObj->oMoveAnglePitch = gMarioState->faceAngle[0];
    gMarioState->marioObj->oMoveAngleYaw = gMarioState->faceAngle[1];
    gMarioState->marioObj->oMoveAngleRoll = gMarioState->faceAngle[2];

    vec3f_copy(gMarioState->marioObj->header.gfx.pos, gMarioState->pos);
    vec3s_set(gMarioState->marioObj->header.gfx.angle, 0, gMarioState->faceAngle[1], 0);

    if (save_file_get_cap_pos(capPos)) {
        capObject = spawn_object(gMarioState->marioObj, MODEL_MARIOS_CAP, bhvNormalCap);

        capObject->oPosX = capPos[0];
        capObject->oPosY = capPos[1];
        capObject->oPosZ = capPos[2];

        capObject->oForwardVelS32 = 0;

        capObject->oMoveAngleYaw = 0;
    }
}

void init_mario_from_save_file(void) {
    gMarioState->unk00 = 0;
    gMarioState->flags = 0;
    gMarioState->action = 0;
    gMarioState->spawnInfo = &gPlayerSpawnInfos[0];
    gMarioState->statusForCamera = &gPlayerCameraState[0];
    gMarioState->marioBodyState = &gBodyStates[0];
    gMarioState->controller = &gControllers[0];
    gMarioState->animation = &D_80339D10;

    gMarioState->numCoins = 0;
    gMarioState->numStars =
        save_file_get_total_star_count(gCurrSaveFileNum - 1, COURSE_MIN - 1, COURSE_MAX - 1);
    gMarioState->numKeys = 0;

    gMarioState->numLives = 4;
    gMarioState->health = 0x880;

    gMarioState->prevNumStarsForDialog = gMarioState->numStars;
    gMarioState->unkB0 = 0xBD;

    gHudDisplay.coins = 0;
    gHudDisplay.wedges = 8;
}
