#include "mario_hat.h"

#ifdef IOS_HAT_MOVEMENT

#include <math.h>
#include <ultra64.h>

#include "sm64.h"
#include "area.h"
#include "audio/external.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_partition.h"
#include "level_update.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "spawn_object.h"
#include "ios_hat.h"
#include "memory.h"

#define MIN_AIM_MAG 8.0f
#define GROUNDED_HAT_HEIGHT 96.0f
#define AIR_HAT_DROP 76.0f
#define MIN_FLOOR_CLEARANCE 42.0f
#define CATCH_HEIGHT 120.0f
#define HAT_POSE_TICKS 5

// A render object only. The hat owns no stock cap interaction or hitbox.
static const BehaviorScript sHatVisual[] = {
    _SHIFTL(OBJ_LIST_DEFAULT, 16, 8), // BEGIN
    _SHIFTL(0x08, 24, 8),            // BEGIN_LOOP
    _SHIFTL(0x09, 24, 8),            // END_LOOP
};

static struct IOSHat sHat;
static struct Object *sHatObject;
static u32 sThrowSourceAction;
static unsigned sPoseTicks;

static bool hat_visual_alive(void) {
    return sHatObject != NULL && (sHatObject->activeFlags & ACTIVE_FLAG_ACTIVE)
        && sHatObject->behavior == segmented_to_virtual(sHatVisual);
}

static void remove_hat_visual(void) {
    if (hat_visual_alive()) {
        sHatObject->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
        mark_obj_for_deletion(sHatObject);
    }
    sHatObject = NULL;
}

bool mario_hat_is_away(void) {
    return sHat.phase != IOS_HAT_ATTACHED && hat_visual_alive();
}

static bool has_normal_hat(const struct MarioState *m) {
    const u32 capMask = MARIO_CAP_FLAGS | MARIO_CAP_ON_HEAD | MARIO_CAP_IN_HAND;
    return (m->flags & capMask) == (MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD);
}

static void cancel_hat(struct MarioState *m) {
    const bool wasAway = sHat.phase != IOS_HAT_ATTACHED;
    const bool usedBounce = sHat.bouncedThisAirTime;
    remove_hat_visual();
    ios_hat_reset(&sHat);
    sHat.bouncedThisAirTime = usedBounce;
    sPoseTicks = 0;

    // Warp and unload hooks may run after the normal Mario render update.
    if (wasAway && m != NULL && m->marioBodyState != NULL && has_normal_hat(m)) {
        m->marioBodyState->capState = MARIO_HAS_DEFAULT_CAP_ON;
    }
}

void mario_hat_reset(void) {
    cancel_hat(gMarioState);
    sHat.bouncedThisAirTime = false;
}

static bool can_keep_hat_out(const struct MarioState *m) {
    const u32 group = m->action & ACT_GROUP_MASK;
    return gCurrentArea != NULL && m->marioObj != NULL && m->floor != NULL
        && m->health >= 0x100 && has_normal_hat(m) && sDelayedWarpOp == WARP_OP_NONE
        && sWarpDest.type == WARP_TYPE_NOT_WARPING
        && !gWarpTransition.isActive && !(m->flags & MARIO_TELEPORTING)
        && !(m->action & (ACT_FLAG_INVULNERABLE | ACT_FLAG_INTANGIBLE | ACT_FLAG_RIDING_SHELL))
        && m->riddenObj == NULL && m->heldObj == NULL
        && m->action != ACT_IOS_KART_GROUND && m->action != ACT_IOS_KART_AIR
        && m->pos[1] >= m->waterLevel - 100.0f
        && (group == ACT_GROUP_STATIONARY || group == ACT_GROUP_MOVING
            || group == ACT_GROUP_AIRBORNE);
}

static bool normal_air_action(u32 action) {
    switch (action) {
        case ACT_JUMP:
        case ACT_DOUBLE_JUMP:
        case ACT_TRIPLE_JUMP:
        case ACT_BACKFLIP:
        case ACT_SIDE_FLIP:
        case ACT_LONG_JUMP:
        case ACT_WALL_KICK_AIR:
        case ACT_FREEFALL:
            return true;
    }
    return false;
}

static bool ground_action_can_throw(u32 action) {
    switch (action) {
        case ACT_IDLE:
        case ACT_PANTING:
        case ACT_STANDING_AGAINST_WALL:
        case ACT_WALKING:
        case ACT_BRAKING:
        case ACT_DECELERATING:
        case ACT_TURNING_AROUND:
        case ACT_FINISH_TURNING_AROUND:
        case ACT_JUMP_LAND:
        case ACT_DOUBLE_JUMP_LAND:
        case ACT_TRIPLE_JUMP_LAND:
        case ACT_FREEFALL_LAND:
        case ACT_JUMP_LAND_STOP:
        case ACT_DOUBLE_JUMP_LAND_STOP:
        case ACT_FREEFALL_LAND_STOP:
            return true;
    }
    return false;
}

static bool hat_point_valid(const float pos[3]) {
    return surface_partition_float_in_bounds(pos[0]) && surface_partition_float_in_bounds(pos[2])
        && pos[1] >= -32768.0f && pos[1] < 32767.0f;
}

static bool hat_path_blocked(const float from[3], const float to[3]) {
    if (!hat_point_valid(from) || !hat_point_valid(to)) return true;

    Vec3f origin = { from[0], from[1], from[2] };
    Vec3f direction = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    Vec3f hitPosition;
    struct Surface *hit = NULL;
    // Short rays stay within the partition's 50 unit surface overlap.
    const f32 span = fmaxf(fabsf(direction[0]), fabsf(direction[2]));
    const int segments = 1 + (int)(span / 40.0f);
    if (span != 0.0f || direction[1] != 0.0f) {
        for (int axis = 0; axis < 3; ++axis) direction[axis] /= (f32)segments;
        for (int i = 0; i < segments; ++i) {
            find_surface_on_ray(origin, direction, &hit, hitPosition);
            if (hit != NULL) return true;
            for (int axis = 0; axis < 3; ++axis) origin[axis] += direction[axis];
        }
    }

    // Cover the destination's collision cell and keep the visual out of walls.
    f32 x = to[0], y = to[1], z = to[2];
    struct Surface *floor;
    const f32 floorY = find_floor(x, y, z, &floor);
    return (floor != NULL && y < floorY + 16.0f)
        || (y + 16.0f > find_ceil(x, y, z, &hit) && hit != NULL)
        || f32_find_wall_collision(&x, &y, &z, 0.0f, 28.0f) != 0;
}

static void update_hat_visual(void) {
    sHatObject->oPosX = sHat.pos[0];
    sHatObject->oPosY = sHat.pos[1];
    sHatObject->oPosZ = sHat.pos[2];
    vec3f_copy(sHatObject->header.gfx.pos, sHat.pos);
    sHatObject->header.gfx.angle[1] += 0x1600;
}

static void step_hat(struct MarioState *m) {
    if (sHat.phase == IOS_HAT_ATTACHED) return;

    const Vec3f target = { m->pos[0], m->pos[1] + CATCH_HEIGHT, m->pos[2] };
    ios_hat_step(&sHat, target, hat_path_blocked);
    if (sHat.phase == IOS_HAT_ATTACHED) {
        remove_hat_visual();
    } else {
        update_hat_visual();
    }
}

// This action emits the throw and resumes the same locomotion action in this
// tick. Keep its state and timers intact so A, B, Z and camera input still go
// through the original action handler, including on the throw frame.
void mario_hat_execute_throw(struct MarioState *m) {
    m->action = sThrowSourceAction;
    const bool airborne = normal_air_action(m->action);

    s16 yaw = m->faceAngle[1];
    if (m->intendedMag >= MIN_AIM_MAG) {
        yaw = m->intendedYaw;
    } else if (m->forwardVel < -MIN_AIM_MAG) {
        yaw += 0x8000;
    }

    const f32 height = airborne
        ? fmaxf(m->pos[1] - AIR_HAT_DROP, m->floorHeight + MIN_FLOOR_CLEARANCE)
        : m->pos[1] + GROUNDED_HAT_HEIGHT;
    const Vec3f start = { m->pos[0], height, m->pos[2] };
    if (!hat_point_valid(start) || gLoadedGraphNodes[MODEL_MARIOS_CAP] == NULL
        || hat_path_blocked(start, start)) return;

    // Regular spawn_object can hang on an exhausted object pool.
    sHatObject = try_to_spawn_object(0, 1.25f, m->marioObj, MODEL_MARIOS_CAP, sHatVisual);
    if (sHatObject == NULL) return;
    const f32 aimSpeed = m->forwardVel * coss(yaw - m->faceAngle[1]);
    if (!ios_hat_launch(&sHat, start, sins(yaw), coss(yaw), airborne ? aimSpeed * 0.25f : aimSpeed)) {
        remove_hat_visual();
        return;
    }

    vec3s_set(sHatObject->header.gfx.angle, 0, yaw, 0);
    sPoseTicks = HAT_POSE_TICKS;
    step_hat(m);
    if (mario_hat_is_away()) {
        play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
    }
}

void mario_hat_update(struct MarioState *m) {
    const bool grounded = ground_action_can_throw(m->action);
    if (!can_keep_hat_out(m)) {
        cancel_hat(m);
        return;
    }

    const bool airborne = normal_air_action(m->action);

    // A lost render object removes the platform as well.
    if (sHat.phase != IOS_HAT_ATTACHED && !hat_visual_alive()) {
        cancel_hat(m);
    }

    if (sHat.phase == IOS_HAT_ATTACHED && (grounded || airborne)
        && m->controller != NULL && (m->controller->buttonPressed & HAT_BUTTON)) {
        sThrowSourceAction = m->action;
        m->action = ACT_IOS_HAT_THROW;
        return;
    }
    step_hat(m);
}

void mario_hat_finish_frame(struct MarioState *m) {
    if (!can_keep_hat_out(m)) {
        cancel_hat(m);
        return;
    }
    if (sPoseTicks > 0) {
        --sPoseTicks;
        if (m->action != sThrowSourceAction || !mario_hat_is_away()) {
            sPoseTicks = 0;
        } else {
            m->marioBodyState->handState = MARIO_HAND_OPEN;
        }
    }
}

void mario_hat_on_landing(const struct MarioState *m) {
    if (m == gMarioState) {
        sHat.bouncedThisAirTime = false;
    }
}

bool mario_hat_try_bounce(struct MarioState *m, const float previous[3],
                          float startTime, float endTime) {
    if (m != gMarioState || !mario_hat_is_away() || !normal_air_action(m->action)
        || !can_keep_hat_out(m)
        || !ios_hat_bounce(&sHat, previous, m->pos, startTime, endTime, &m->vel[1], hat_path_blocked)) {
        return false;
    }

    // Retain the horizontal velocity and jump action. The new apex starts
    // here, so a fall before the bounce doesn't count against a later landing.
    m->peakHeight = m->pos[1];
    m->flags &= ~MARIO_UNKNOWN_08;
    play_sound(SOUND_ACTION_BOUNCE_OFF_OBJECT, m->marioObj->header.gfx.cameraToObject);
    return true;
}

#endif
