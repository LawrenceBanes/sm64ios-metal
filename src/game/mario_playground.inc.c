#include "engine/surface_partition.h"

/* Session settings live in CheatList. This state belongs to the game thread. */
static unsigned sPlaygroundHat;
static bool sPlaygroundFlying;
static bool sPlaygroundBowser;

static bool playground_compatible(struct MarioState *m) {
    u32 group = m->action & ACT_GROUP_MASK;
    return gCurrentArea && m->marioObj && m->controller && m->floor && m->health >= 0x100
        && sDelayedWarpOp == WARP_OP_NONE && sWarpDest.type == WARP_TYPE_NOT_WARPING
        && !gWarpTransition.isActive && !(m->flags & MARIO_TELEPORTING)
        && !(m->action & (ACT_FLAG_INTANGIBLE | ACT_FLAG_INVULNERABLE | ACT_FLAG_RIDING_SHELL))
        && !m->heldObj && !m->riddenObj
        && m->action != ACT_IOS_KART_GROUND && m->action != ACT_IOS_KART_AIR
        && (group == ACT_GROUP_STATIONARY || group == ACT_GROUP_MOVING || group == ACT_GROUP_AIRBORNE);
}

static void playground_hat(struct MarioState *m) {
    unsigned hat = cheats_playground_get() & (CHEAT_HAT_ON | CHEAT_HAT_OFF);
    if (hat != sPlaygroundHat) {
#ifdef IOS_HAT_MOVEMENT
        mario_hat_reset();
#endif
        if (hat == CHEAT_HAT_ON) {
            struct Object *cap;
            f32 distance;
            while ((cap = cur_obj_find_nearest_object_with_behavior(bhvNormalCap, &distance)) != NULL) {
                cap->activeFlags = ACTIVE_FLAG_DEACTIVATED;
            }
        }
        if (hat == 0 && sPlaygroundHat == CHEAT_HAT_OFF) {
            m->flags |= MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD;
        }
        sPlaygroundHat = hat;
    }
    if (hat == CHEAT_HAT_OFF) {
        m->flags &= ~(MARIO_CAP_ON_HEAD | MARIO_CAP_IN_HAND);
    } else if (hat == CHEAT_HAT_ON) {
        u32 lost = SAVE_FLAG_CAP_ON_GROUND | SAVE_FLAG_CAP_ON_KLEPTO
                 | SAVE_FLAG_CAP_ON_UKIKI | SAVE_FLAG_CAP_ON_MR_BLIZZARD;
        if (gCurrSaveFileNum >= 1 && gCurrSaveFileNum <= 4 && (save_file_get_flags() & lost)) {
            save_file_clear_flags(lost);
        }
        m->flags |= MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD;
        m->flags &= ~MARIO_CAP_IN_HAND;
    }
}

/* Flight retains horizontal momentum when the stick is released. A/Z set
 * vertical thrust. Velocity remains bounded in game ticks (SM64 runs at 30 Hz). */
#define PLAYGROUND_FLIGHT_ACCEL 2.0f
#define PLAYGROUND_FLIGHT_MAX_SPEED 48.0f
#define PLAYGROUND_FLIGHT_VERTICAL_SPEED 24.0f

static bool playground_step(struct MarioState *m) {
    unsigned settings = cheats_playground_get();
    bool safe = playground_compatible(m);
    sPlaygroundBowser = safe && (settings & CHEAT_BOWSER);
    if (!safe) {
        sPlaygroundFlying = false;
        return false;
    }
    if (!(settings & CHEAT_FLIGHT)) {
        if (sPlaygroundFlying) {
            set_mario_action(m, ACT_FREEFALL, 0);
            m->vel[1] = 0;
        }
        sPlaygroundFlying = false;
        return false;
    }
#ifdef IOS_HAT_MOVEMENT
    mario_hat_reset();
#endif
    if (!sPlaygroundFlying) {
        set_mario_action(m, ACT_FLYING, 0);
        vec3f_set(m->vel, 0, 0, 0);
        m->forwardVel = 0;
    }
    sPlaygroundFlying = true;
    m->action = ACT_FLYING;
    if (m->intendedMag > 0) {
        f32 targetSpeed = PLAYGROUND_FLIGHT_MAX_SPEED * fminf(m->intendedMag / 32.0f, 1);
        f32 x = targetSpeed * sins(m->intendedYaw);
        f32 z = targetSpeed * coss(m->intendedYaw);
        m->vel[0] = approach_f32(m->vel[0], x, PLAYGROUND_FLIGHT_ACCEL, PLAYGROUND_FLIGHT_ACCEL);
        m->vel[2] = approach_f32(m->vel[2], z, PLAYGROUND_FLIGHT_ACCEL, PLAYGROUND_FLIGHT_ACCEL);
        m->faceAngle[1] = m->intendedYaw;
    }
    f32 speed = sqrtf(m->vel[0]*m->vel[0] + m->vel[2]*m->vel[2]);
    if (speed > PLAYGROUND_FLIGHT_MAX_SPEED) {
        m->vel[0] *= PLAYGROUND_FLIGHT_MAX_SPEED / speed;
        m->vel[2] *= PLAYGROUND_FLIGHT_MAX_SPEED / speed;
    }
    m->forwardVel = fminf(speed, PLAYGROUND_FLIGHT_MAX_SPEED);
    m->vel[1] = ((m->controller->buttonDown & A_BUTTON) != 0)
             * PLAYGROUND_FLIGHT_VERTICAL_SPEED
             - ((m->controller->buttonDown & Z_TRIG) != 0) * PLAYGROUND_FLIGHT_VERTICAL_SPEED;
    if (m->pos[1] < m->waterLevel - 80) {
        set_mario_action(m, ACT_WATER_IDLE, 0);
        sPlaygroundFlying = false;
        return true;
    }
    if (!surface_partition_float_in_bounds(m->pos[0] + m->vel[0])) m->vel[0] = 0;
    if (!surface_partition_float_in_bounds(m->pos[2] + m->vel[2])) m->vel[2] = 0;
    if (fabsf(m->pos[1] + m->vel[1]) > 30000.0f) m->vel[1] = 0;
    s32 result = perform_air_step(m, 0);
    if (result == AIR_STEP_HIT_WALL || result == AIR_STEP_HIT_LAVA_WALL) {
        m->vel[0] = m->vel[2] = m->forwardVel = 0;
    }
    m->faceAngle[0] = 0;
    set_mario_animation(m, MARIO_ANIM_WING_CAP_FLY);
    return true;
}

static void playground_bowser_movement(struct MarioState *m) {
    if (!sPlaygroundBowser) return;
    /* Small playable Bowser uses Mario's collision capsule and a deliberate
     * slower ground gait. Stock traversal and interaction remain authoritative. */
    if (!(m->action & ACT_FLAG_AIR) && fabsf(m->forwardVel) > 24.0f) {
        mario_set_forward_vel(m, m->forwardVel < 0 ? -24.0f : 24.0f);
    }
}

s32 mario_playground_bowser_active(void) {
    return sPlaygroundBowser;
}
