/* Native adaptation of SuperSlide 1.1 by SkyCaster09, supplied by the user.
 * See ios/SUPERSLIDE_HANDOFF.md for source identity and adaptation details. */
#ifdef IOS_BUILD
#include "mario_superslide.h"
#include "area.h"
#include "audio/external.h"
#include "engine/math_util.h"
#include "game_init.h"
#include "level_update.h"
#include "mario.h"
#include "mario_actions_moving.h"
#include "mario_hat.h"
#include "mario_step.h"
#include "pc/cheats.h"
#include "sm64.h"
#include <math.h>
#include <stdbool.h>
#include <ultra64.h>


static f32 sLastSpeed;
static bool sFirstLavaSkip;
static u16 sEntryButtons;

void mario_superslide_reset(void) {
    sEntryButtons = 0;
    sLastSpeed = 0;
    sFirstLavaSkip = false;
}

static bool enabled(struct MarioState *m) {
    unsigned settings = cheats_playground_get();
    return (settings & CHEAT_SUPERSLIDE) && !(settings & (CHEAT_FLIGHT | CHEAT_BOWSER)) && gCurrentArea &&
           m->marioObj && m->controller && m->floor && m->health >= 0x100 && !m->heldObj && !m->riddenObj &&
           !(m->flags & MARIO_TELEPORTING) && !gWarpTransition.isActive && sDelayedWarpOp == WARP_OP_NONE &&
           sWarpDest.type == WARP_TYPE_NOT_WARPING;
}

void mario_superslide_update(struct MarioState *m) {
    if (!enabled(m)) {
        if (m->action == ACT_IOS_SUPERSLIDE)
            set_mario_action(m, ACT_FREEFALL, 0);
        mario_superslide_reset();
        return;
    }
#ifdef IOS_HAT_MOVEMENT
    if (mario_hat_is_away())
        return;
#endif
    if (m->action == ACT_SLIDE_KICK) {
        set_mario_action(m, ACT_IOS_SUPERSLIDE, 0);
        m->forwardVel += 10;
        sFirstLavaSkip = true;
        sEntryButtons = m->controller->buttonPressed;
    } else if (m->action == ACT_GROUND_POUND && (m->controller->buttonPressed & B_BUTTON)) {
        set_mario_action(m, ACT_IOS_SUPERSLIDE, 0);
        m->forwardVel = fmaxf(15, sLastSpeed);
        m->vel[1] = 10;
        sFirstLavaSkip = true;
        sEntryButtons = m->controller->buttonPressed;
        play_sound(SOUND_MARIO_HOOHOO, m->marioObj->header.gfx.cameraToObject);
    } else if (m->action != ACT_GROUND_POUND) {
        sLastSpeed = m->forwardVel;
        if (m->action != ACT_IOS_SUPERSLIDE)
            sFirstLavaSkip = false;
    }
}

static void slide_feedback(struct MarioState *m) {
    m->particleFlags |= m->forwardVel > 80 ? PARTICLE_SPARKLES : PARTICLE_DUST;
    if (!(gGlobalTimer & 1)) {
        play_sound(SOUND_MOVING_TERRAIN_SLIDE, m->marioObj->header.gfx.cameraToObject);
    }
}

static f32 horizontal_speed(struct MarioState *m) {
    return sqrtf(m->vel[0] * m->vel[0] + m->vel[2] * m->vel[2]);
}

static void limit_speed(struct MarioState *m) {
    f32 cap = cheats_superslide_speed();
    f32 speed = horizontal_speed(m);
    if (!isfinite(speed)) {
        m->vel[0] = m->vel[2] = m->forwardVel = 0;
    } else if (speed > cap) {
        m->vel[0] *= cap / speed;
        m->vel[2] *= cap / speed;
        m->forwardVel = cap;
    }
}

static s32 wall_slide(struct MarioState *m, u16 pressed) {
    if (!m->wall)
        return FALSE;
    f32 length = sqrtf(m->vel[0] * m->vel[0] + m->vel[1] * m->vel[1] + m->vel[2] * m->vel[2]);
    f32 dot = length > 0 ? (m->vel[0] * m->wall->normal.x + m->vel[1] * m->wall->normal.y +
                            m->vel[2] * m->wall->normal.z) /
                               length
                         : 0;
    if (dot < -0.8f) {
        m->particleFlags |= PARTICLE_VERTICAL_STAR;
        return set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
    }
    if (m->pos[1] <= m->floorHeight + 2)
        return FALSE;
    if (m->forwardVel < 5)
        return set_mario_action(m, ACT_FREEFALL, 0);
    slide_feedback(m);
    f32 cross = sins(m->faceAngle[1]) * m->wall->normal.z - coss(m->faceAngle[1]) * m->wall->normal.x;
    s16 yaw = atan2s(m->wall->normal.z, m->wall->normal.x);
    m->faceAngle[1] = yaw + (cross > 0 ? 0x4800 : -0x4800);
    if (pressed & A_BUTTON) {
        m->faceAngle[1] = yaw + (cross > 0 ? 0x1c00 : -0x1c00);
        set_mario_action(m, ACT_WALL_KICK_AIR, 0);
        m->forwardVel += 10;
        m->vel[1] = 55;
        m->particleFlags |= PARTICLE_VERTICAL_STAR;
        play_sound(SOUND_ACTION_HIT, m->marioObj->header.gfx.cameraToObject);
        return TRUE;
    }
    if (pressed & B_BUTTON)
        return set_mario_action(m, ACT_FREEFALL, 0);
    return FALSE;
}

s32 mario_act_superslide(struct MarioState *m) {
    if (!enabled(m))
        return set_mario_action(m, ACT_FREEFALL, 0);
    // Consume only the entry edge locally. Other actions retain their input.
    u16 pressed = m->controller->buttonPressed & ~sEntryButtons;
    sEntryButtons = 0;
    set_mario_animation(m, MARIO_ANIM_SLIDE_KICK);
    m->forwardVel = fminf(fmaxf(m->forwardVel, 0), cheats_superslide_speed());
    m->vel[0] = sins(m->faceAngle[1]) * m->forwardVel;
    m->vel[2] = coss(m->faceAngle[1]) * m->forwardVel;
    if (m->pos[1] <= m->waterLevel && m->forwardVel > 45) {
        m->pos[1] = m->waterLevel + 1;
        m->vel[1] = 17;
        m->forwardVel -= 10;
        m->particleFlags |= PARTICLE_WATER_SPLASH;
        play_sound(SOUND_ACTION_QUICKSAND_STEP, m->marioObj->header.gfx.cameraToObject);
    }
    if (m->floor->type == SURFACE_BURNING && m->pos[1] < m->floorHeight + 1) {
        if (m->forwardVel <= 45 && !sFirstLavaSkip)
            return set_mario_action(m, ACT_FREEFALL, 0);
        if (!sFirstLavaSkip)
            m->forwardVel -= 10;
        sFirstLavaSkip = false;
        m->pos[1] = m->floorHeight + 5;
        m->vel[1] = 17;
        m->particleFlags |= PARTICLE_HORIZONTAL_STAR;
        play_sound(SOUND_ACTION_QUICKSAND_STEP, m->marioObj->header.gfx.cameraToObject);
    }

    f32 dot = coss((s16)(m->intendedYaw - m->faceAngle[1]));
    if (m->pos[1] > m->floorHeight + 2 || m->vel[1] > 0) {
        if (m->forwardVel > 80)
            m->particleFlags |= PARTICLE_SPARKLES;
        f32 previous = m->forwardVel;
        m->vel[0] += sins(m->intendedYaw) * m->intendedMag * 0.1f;
        m->vel[2] += coss(m->intendedYaw) * m->intendedMag * 0.1f;
        if (dot > -0.8f) {
            m->faceAngle[1] =
                m->intendedYaw - approach_s32((s16)(m->intendedYaw - m->faceAngle[1]), 0, 0x90, 0x90);
        }
        m->forwardVel = horizontal_speed(m);
        if (m->forwardVel > 45 && m->forwardVel > previous) {
            f32 ratio = previous / m->forwardVel;
            m->vel[0] *= ratio;
            m->vel[2] *= ratio;
            m->forwardVel = previous;
        }
        limit_speed(m);
        perform_air_step(m, 0);
        if (wall_slide(m, pressed))
            return TRUE;
        if (!m->wall) {
            m->vel[1] += 2; // Net gravity of two units per game tick.
            if (pressed & A_BUTTON) {
                set_mario_action(m, ACT_JUMP_KICK, 0);
                m->vel[1] = 20;
                return TRUE;
            }
        }
    } else {
        slide_feedback(m);
        m->vel[1] = 0;
        m->vel[0] += m->floor->normal.x * 5;
        m->vel[2] += m->floor->normal.z * 5;
        limit_speed(m);
        perform_ground_step(m);
        if (dot < 0 && m->intendedMag > 8 && m->forwardVel > 0) {
            f32 old = m->forwardVel;
            m->forwardVel = fmaxf(0, old + dot * m->intendedMag / 64);
            f32 ratio = m->forwardVel / old;
            m->vel[0] *= ratio * ratio;
            m->vel[2] *= ratio * ratio;
        }
        if (dot > -0.9f) {
            m->faceAngle[1] =
                m->intendedYaw - approach_s32((s16)(m->intendedYaw - m->faceAngle[1]), 0, 0x280, 0x270);
        }
        if (pressed & B_BUTTON)
            return set_mario_action(m, ACT_FORWARD_ROLLOUT, 0);
        if (pressed & A_BUTTON) {
            f32 speed = m->forwardVel;
            set_mario_action(m, ACT_DOUBLE_JUMP, 0);
            m->forwardVel = speed;
            return TRUE;
        }
        m->forwardVel = horizontal_speed(m);
        limit_speed(m);
        if (m->forwardVel < 4)
            return set_mario_action(m, ACT_SLIDE_KICK_SLIDE_STOP, 0);
        if (wall_slide(m, pressed))
            return TRUE;
    }
    if (m->floor && m->pos[1] == m->floorHeight) {
        align_with_floor(m);
    } else {
        m->marioObj->header.gfx.angle[0] = (s16)fminf(atan2s(m->vel[1], m->forwardVel) - 0x4000, 4000);
    }
    return FALSE;
}
#endif
