#include "engine/math_util.h"
#include "game/area.h"
#include "game/level_update.h"
#include "game/mario.h"
#include "game/mario_step.h"
#include "game/mario_superslide.h"
#include "pc/cheats.h"
#include "sm64.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ultra64.h>

struct Area *gCurrentArea;
struct WarpDest sWarpDest;
struct WarpTransition gWarpTransition;
s16 sDelayedWarpOp;
u32 gGlobalTimer;
static struct MarioState mario;
static struct Object object;
static struct Surface ground, wall;
static struct Controller input;
static struct Area area;
static bool touchWall, hatAway;

bool mario_hat_is_away(void) {
    return hatAway;
}
s16 set_mario_animation(struct MarioState *m, s32 anim) {
    assert(anim == MARIO_ANIM_SLIDE_KICK);
    return 0;
}
u32 set_mario_action(struct MarioState *m, u32 action, u32 arg) {
    m->action = action;
    return TRUE;
}
void play_sound(s32 sound, f32 *pos) {
    assert(pos == object.header.gfx.cameraToObject);
}
void align_with_floor(struct MarioState *m) {
}
s32 perform_ground_step(struct MarioState *m) {
    m->pos[0] += m->vel[0];
    m->pos[2] += m->vel[2];
    m->wall = touchWall ? &wall : NULL;
    return touchWall ? GROUND_STEP_HIT_WALL : GROUND_STEP_NONE;
}
s32 perform_air_step(struct MarioState *m, u32 arg) {
    for (int i = 0; i < 3; ++i)
        m->pos[i] += m->vel[i];
    m->vel[1] -= 4;
    m->wall = touchWall ? &wall : NULL;
    return touchWall ? AIR_STEP_HIT_WALL : AIR_STEP_NONE;
}
static struct MarioState *fixture(void) {
    memset(&mario, 0, sizeof(mario));
    memset(&ground, 0, sizeof(ground));
    memset(&wall, 0, sizeof(wall));
    memset(&input, 0, sizeof(input));
    memset(&gWarpTransition, 0, sizeof(gWarpTransition));
    sWarpDest.type = WARP_TYPE_NOT_WARPING;
    sDelayedWarpOp = WARP_OP_NONE;
    gCurrentArea = &area;
    mario.floor = &ground;
    mario.controller = &input;
    mario.marioObj = &object;
    mario.health = 0x880;
    mario.waterLevel = -10000;
    mario.action = ACT_SLIDE_KICK;
    mario.forwardVel = 30;
    ground.normal.y = 1;
    touchWall = hatAway = false;
    cheats_playground_set(~0u, CHEAT_SUPERSLIDE);
    cheats_set_superslide_speed(90);
    mario_superslide_reset();
    mario_superslide_update(&mario);
    assert(mario.action == ACT_IOS_SUPERSLIDE && mario.forwardVel == 40);
    return &mario;
}
int main(void) {
    struct MarioState *m = fixture();
    cheats_set_superslide_speed(1);
    assert(cheats_superslide_speed() == 30);
    cheats_set_superslide_speed(999);
    assert(cheats_superslide_speed() == 300);
    cheats_set_superslide_speed(90);
    m->action = ACT_WALKING;
    m->forwardVel = 73;
    mario_superslide_update(m);
    m->action = ACT_GROUND_POUND;
    m->forwardVel = 0;
    input.buttonPressed = B_BUTTON;
    mario_superslide_update(m);
    assert(m->action == ACT_IOS_SUPERSLIDE && m->forwardVel == 73 && m->vel[1] == 10);
    mario_act_superslide(m);
    assert(m->action == ACT_IOS_SUPERSLIDE); // Entry B did not roll out.
    m = fixture();
    m->action = ACT_SLIDE_KICK;
    m->vel[1] = 12;
    input.buttonPressed = B_BUTTON;
    mario_superslide_update(m);
    mario_act_superslide(m);
    assert(m->action == ACT_IOS_SUPERSLIDE && m->pos[1] == 12);
    m = fixture();
    ground.normal.z = 0.5f;
    mario_act_superslide(m);
    assert(m->forwardVel == 42.5f);
    for (int i = 0; i < 1000; ++i) {
        mario_act_superslide(m);
        assert(m->forwardVel <= 90 && isfinite(m->pos[2]));
    }
    m = fixture();
    input.buttonPressed = A_BUTTON;
    mario_act_superslide(m);
    assert(m->action == ACT_DOUBLE_JUMP && m->forwardVel == 40);
    m = fixture();
    input.buttonPressed = B_BUTTON;
    mario_act_superslide(m);
    assert(m->action == ACT_FORWARD_ROLLOUT);
    m = fixture();
    m->forwardVel = 0;
    m->intendedMag = 0;
    mario_act_superslide(m);
    assert(m->action == ACT_SLIDE_KICK_SLIDE_STOP);
    m = fixture();
    m->pos[1] = 100;
    m->intendedMag = 32;
    m->intendedYaw = 0x4000;
    mario_act_superslide(m);
    assert(m->faceAngle[1] == 0x90 && m->forwardVel > 40 && m->forwardVel < 45);
    m = fixture();
    cheats_set_superslide_speed(30);
    mario_act_superslide(m);
    assert(m->forwardVel == 30);
    m = fixture();
    m->pos[1] = 100;
    m->forwardVel = 60;
    m->intendedMag = 32;
    mario_act_superslide(m);
    assert(m->forwardVel == 60 && m->vel[1] == -2);
    input.buttonPressed = A_BUTTON;
    mario_act_superslide(m);
    assert(m->action == ACT_JUMP_KICK && m->vel[1] == 20);
    m = fixture();
    m->intendedYaw = -0x8000;
    m->intendedMag = 32;
    mario_act_superslide(m);
    assert(m->forwardVel < 40 && m->faceAngle[1] == 0);
    m = fixture();
    m->pos[1] = 100;
    touchWall = true;
    wall.normal.x = 1;
    mario_act_superslide(m);
    assert(m->action == ACT_IOS_SUPERSLIDE && m->faceAngle[1] == -0x800);
    input.buttonPressed = A_BUTTON;
    mario_act_superslide(m);
    assert(m->action == ACT_WALL_KICK_AIR && m->vel[1] == 55);
    m = fixture();
    m->pos[1] = 100;
    touchWall = true;
    wall.normal.x = 1;
    input.buttonPressed = B_BUTTON;
    mario_act_superslide(m);
    assert(m->action == ACT_FREEFALL);
    m = fixture();
    m->pos[1] = 100;
    touchWall = true;
    wall.normal.z = -1;
    mario_act_superslide(m);
    assert(m->action == ACT_BACKWARD_AIR_KB);
    m = fixture();
    m->waterLevel = 0;
    m->floorHeight = -200;
    m->forwardVel = 60;
    mario_act_superslide(m);
    assert(m->pos[1] == 18 && m->particleFlags & PARTICLE_WATER_SPLASH);
    m = fixture();
    ground.type = SURFACE_BURNING;
    m->forwardVel = 30;
    mario_act_superslide(m);
    assert(m->pos[1] == 22 && m->action == ACT_IOS_SUPERSLIDE);
    m->pos[1] = 0;
    mario_act_superslide(m);
    assert(m->action == ACT_FREEFALL);
    for (unsigned flag = CHEAT_FLIGHT; flag <= CHEAT_BOWSER; flag *= 2) {
        m = fixture();
        cheats_playground_set(flag, flag);
        mario_superslide_update(m);
        assert(m->action == ACT_FREEFALL);
    }
    m = fixture();
    m->action = ACT_SLIDE_KICK;
    hatAway = true;
    mario_superslide_update(m);
#ifdef IOS_HAT_MOVEMENT
    assert(m->action == ACT_SLIDE_KICK);
#else
    assert(m->action == ACT_IOS_SUPERSLIDE);
#endif
    m = fixture();
    cheats_playground_set(CHEAT_SUPERSLIDE, 0);
    mario_superslide_update(m);
    assert(m->action == ACT_FREEFALL);
    m->action = ACT_SLIDE_KICK;
    mario_superslide_update(m);
    assert(m->action == ACT_SLIDE_KICK);
    m = fixture();
    m->health = 0xff;
    mario_superslide_update(m);
    assert(m->action == ACT_FREEFALL);
    m = fixture();
    gWarpTransition.isActive = true;
    mario_superslide_update(m);
    assert(m->action == ACT_FREEFALL);
    m = fixture();
    gCurrentArea = NULL;
    mario_superslide_update(m);
    assert(m->action == ACT_FREEFALL);
    m = fixture();
    m->floor = NULL;
    assert(mario_act_superslide(m) && m->action == ACT_FREEFALL);
    puts("SuperSlide entries, slope, cap, combos, steering, walls, skips and isolation passed");
    return 0;
}
