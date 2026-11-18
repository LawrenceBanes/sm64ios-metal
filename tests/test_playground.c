#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <ultra64.h>
#include "sm64.h"
#include "game/mario.h"
#include "game/area.h"
#include "game/level_update.h"
#include "game/save_file.h"
#include "game/mario_step.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/mario_hat.h"
#include "engine/math_util.h"
#include "pc/cheats.h"
#include "behavior_data.h"
#include "game/mario_playground.inc.c"
struct MarioState *gMarioState;
struct Object *gMarioObject;
struct Area *gCurrentArea;
struct WarpDest sWarpDest;
struct WarpTransition gWarpTransition;
s16 sDelayedWarpOp;
s16 gCurrSaveFileNum = 1;
static u32 saveFlags;
static unsigned resets;
static int collisionResult;
void mario_hat_reset(void) { ++resets; }
u32 save_file_get_flags(void) { return saveFlags; }
void save_file_clear_flags(u32 flags) { saveFlags &= ~flags; }
struct Object *cur_obj_find_nearest_object_with_behavior(const BehaviorScript *behavior, f32 *distance) { return NULL; }
const BehaviorScript bhvNormalCap[] = {0};
u32 set_mario_action(struct MarioState *m, u32 action, u32 arg) { m->action = action; return 1; }
s16 set_mario_animation(struct MarioState *m, s32 animation) { return 0; }
s32 perform_air_step(struct MarioState *m, u32 arg) {
    for (int i = 0; i < 3; ++i) m->pos[i] += m->vel[i];
    return collisionResult;
}
void mario_set_forward_vel(struct MarioState *m, f32 speed) { m->forwardVel = speed; }
int main(void) {
    struct MarioState m = {0};
    struct Object object = {0};
    struct Controller input = {0};
    struct Surface floor = {0};
    struct Area area = {0};
    gMarioState = &m; gMarioObject = &object; gCurrentArea = &area;
    m.marioObj = &object; m.controller = &input; m.floor = &floor;
    m.health = 0x880; m.waterLevel = -10000; m.action = ACT_IDLE;
    cheats_playground_set(15, 0);
    assert(cheats_playground_get() == 0 && !playground_step(&m));
    const u32 lost[] = {SAVE_FLAG_CAP_ON_GROUND, SAVE_FLAG_CAP_ON_KLEPTO,
                        SAVE_FLAG_CAP_ON_UKIKI, SAVE_FLAG_CAP_ON_MR_BLIZZARD};
    for (unsigned i=0; i<4; ++i) {
        saveFlags = lost[i]; m.flags = 0;
        cheats_playground_set(3, CHEAT_HAT_ON);
        playground_hat(&m);
        assert(!saveFlags && (m.flags & MARIO_NORMAL_CAP) && (m.flags & MARIO_CAP_ON_HEAD));
        cheats_playground_set(3, CHEAT_HAT_OFF);
        playground_hat(&m);
        assert(!(m.flags & (MARIO_CAP_ON_HEAD | MARIO_CAP_IN_HAND)));
    }
    assert(resets == 8);
    cheats_playground_set(15, CHEAT_FLIGHT | CHEAT_BOWSER);
    input.buttonDown = A_BUTTON; m.intendedMag = 32; m.intendedYaw = 0;
    for (int tick = 0; tick < 1000; ++tick) {
        assert(playground_step(&m));
        assert(m.forwardVel <= PLAYGROUND_FLIGHT_MAX_SPEED);
        assert(m.vel[1] == PLAYGROUND_FLIGHT_VERTICAL_SPEED);
        vec3f_set(m.pos, 0, 0, 0);
    }
    m.intendedMag = 0; input.buttonDown = 0;
    float speed = m.forwardVel;
    assert(playground_step(&m) && m.forwardVel == speed && m.vel[1] == 0);
    collisionResult = AIR_STEP_HIT_WALL;
    assert(playground_step(&m) && m.forwardVel == 0);
    collisionResult = AIR_STEP_NONE; input.buttonDown = Z_TRIG;
    assert(playground_step(&m) && m.vel[1] < 0);
    cheats_playground_set(CHEAT_FLIGHT, 0);
    assert(!playground_step(&m) && m.action == ACT_FREEFALL);
    const u32 incompatible[] = {ACT_STANDING_DEATH, ACT_DISAPPEARED, ACT_READING_AUTOMATIC_DIALOG, ACT_WATER_IDLE};
    for (unsigned i=0; i<4; ++i) {
        cheats_playground_set(CHEAT_FLIGHT, CHEAT_FLIGHT);
        m.action = incompatible[i];
        assert(!playground_step(&m) && !mario_playground_bowser_active());
        m.action = ACT_IDLE; assert(playground_step(&m));
    }
    m.pos[1] = 29999; input.buttonDown = A_BUTTON;
    assert(playground_step(&m) && m.vel[1] == 0);
    m.pos[1] = m.waterLevel - 100;
    assert(playground_step(&m) && m.action == ACT_WATER_IDLE && !sPlaygroundFlying);
    m.pos[1] = 0; m.action = ACT_IDLE;
    gWarpTransition.isActive = 1; assert(!playground_step(&m));
    gWarpTransition.isActive = 0; gCurrentArea = NULL; assert(!playground_step(&m));
    gCurrentArea = &area; m.action = ACT_IDLE;
    cheats_playground_set(CHEAT_FLIGHT, 0); playground_step(&m);
    m.forwardVel = 60; playground_bowser_movement(&m); assert(m.forwardVel == 24);
    puts("Playground ownership, bounded flight, suspension and Bowser gait passed");
}
