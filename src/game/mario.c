Warning: truncated output (original token count: 30913)
Total output lines: 3193

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
#include "ios_kart.h"
#include "surface_terrains.h"
#include "game_init.h"
#include "interaction.h"
#include "level_table.h"
#include "level_update.h"
#include "main.h"
#include "mario.h"
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
#include "pc/cheats.h"
#ifdef IOS_BUILD
#include "pc/gfx/gfx_ios_diagnostics.h"
#define IOS_PORTAL_EVENT(...) gfx_ios_diag_log(__VA_ARGS__)
#define IOS_PORTAL_STATUS(message) gfx_ios_portal_status(message)
#define IOS_KART_EVENT(...) gfx_ios_diag_log(__VA_ARGS__)
#define IOS_KART_STATUS(message) gfx_ios_portal_status(message)
#else
#define IOS_PORTAL_EVENT(...) ((void)0)
#define IOS_PORTAL_STATUS(message) ((void)0)
#define IOS_KART_EVENT(...) ((void)0)
#define IOS_KART_STATUS(message) ((void)0)
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
 …20913 tokens truncated…->pos[1] < (gasLevel - 100.0f)) {
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
        mario_handle_special_floors(gMarioState);
        mario_process_interactions(gMarioState);

        // If Mario is OOB, stop executing actions.
        if (gMarioState->floor == NULL) {
            mario_ios_restore_kart_to_last_ground();
            mario_ios_clear_kart_mount(gMarioState);
            return 0;
        }

        mario_ios_apply_gameplay_requests(gMarioState);
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
        while (inLoop) {
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
        set_submerged_cam_preset_and_spawn_bubbles(gMarioState);
        update_mario_health(gMarioState);
        update_mario_info_for_cam(gMarioState);
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
