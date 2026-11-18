#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "sm64.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game/area.h"
#include "game/ios_hat.h"
#include "game/ios_reality.h"
#include "game/level_update.h"
#include "game/mario_hat.h"
#include "game/mario_step.h"
#include "game/object_helpers.h"
#include "game/mario.h"
#include "game/save_file.h"
#include "pc/cheats.h"
#include "behavior_data.h"
#include "game/mario_playground.inc.c"

void unload_objects_from_area(s32 unused, s32 area) {}
void geo_call_global_function_nodes(struct GraphNode *node, s32 context) {}
static u32 savedCapFlags;
u32 save_file_get_flags(void) { return savedCapFlags; }
void save_file_clear_flags(u32 flags) { savedCapFlags &= ~flags; }
const BehaviorScript bhvNormalCap[] = {0};
struct Object *cur_obj_find_nearest_object_with_behavior(const BehaviorScript *behavior, f32 *distance) {
    return NULL;
}


static struct Object marioObject, hatObject;
static struct MarioBodyState body;
static struct Controller controller;
static struct Surface ground;
static struct GraphNode model;
static bool poolFull, obstructed, floorMissing;
static unsigned bounces, throws;
static f32 floorY;

// The harness runs the real action adapter and air step over a simple flat
// floor. These stubs stand in for area geometry, asset and audio services.
void *segmented_to_virtual(const void *p) { return (void *)p; }

struct Object *try_to_spawn_object(s16 y, f32 scale, struct Object *parent, s32 modelId,
                                   const BehaviorScript *behavior) {
    assert(y == 0 && scale > 1 && parent == &marioObject && modelId == MODEL_MARIOS_CAP);
    if (poolFull) return NULL;
    assert(hatObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
    memset(&hatObject, 0, sizeof(hatObject));
    hatObject.activeFlags = ACTIVE_FLAG_ACTIVE;
    hatObject.behavior = behavior;
    return &hatObject;
}

void mark_obj_for_deletion(struct Object *obj) { obj->activeFlags = ACTIVE_FLAG_DEACTIVATED; }

void play_sound(s32 bits, f32 *pos) {
    assert(pos == marioObject.header.gfx.cameraToObject);
    if (bits == SOUND_ACTION_SPIN) ++throws;
    if (bits == SOUND_ACTION_BOUNCE_OFF_OBJECT) ++bounces;
}

f32 find_floor(f32 x, f32 y, f32 z, struct Surface **floor) {
    (void)x; (void)y; (void)z;
    *floor = floorMissing ? NULL : &ground;
    return floorY;
}

f32 find_ceil(f32 x, f32 y, f32 z, struct Surface **ceil) {
    (void)x; (void)y; (void)z;
    *ceil = NULL;
    return 20000.0f;
}

f32 vec3f_find_ceil(Vec3f pos, f32 floorHeight, struct Surface **ceil) {
    return find_ceil(pos[0], floorHeight, pos[2], ceil);
}

f32 find_water_level(f32 x, f32 z) {
    (void)x; (void)z;
    return -11000.0f;
}

void find_surface_on_ray(Vec3f origin, Vec3f direction, struct Surface **hit, Vec3f position) {
    (void)origin; (void)direction; (void)position;
    *hit = obstructed ? &ground : NULL;
}

s32 f32_find_wall_collision(f32 *x, f32 *y, f32 *z, f32 offset, f32 radius) {
    (void)x; (void)y; (void)z; (void)offset; (void)radius;
    return 0;
}

struct Surface *resolve_and_return_wall_collisions(Vec3f pos, f32 offset, f32 radius) {
    (void)pos; (void)offset; (void)radius;
    return NULL;
}

u32 mario_get_terrain_sound_addend(struct MarioState *m) { (void)m; return 0; }

const IOSRealityPolicy *ios_reality_global_policy(void) {
    static const IOSRealityPolicy policy = { .gravity_multiplier = 1.0f };
    return &policy;
}

static struct MarioState *fixture(void) {
    static struct Area area = { .index = 1 };
    mario_hat_reset();
    gCurrentArea = &area;
    sWarpDest.type = WARP_TYPE_NOT_WARPING;
    sDelayedWarpOp = WARP_OP_NONE;
    poolFull = obstructed = floorMissing = false;
    floorY = 0.0f;
    throws = bounces = 0;
    gLoadedGraphNodes[MODEL_MARIOS_CAP] = &model;

    body.capState = MARIO_HAS_DEFAULT_CAP_ON;
    *gMarioState = (struct MarioState) {
        .action = ACT_IDLE, .flags = MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD,
        .health = 0x880, .marioObj = &marioObject, .marioBodyState = &body,
        .controller = &controller, .floor = &ground, .waterLevel = -11000.0f,
    };
    return gMarioState;
}

static void throw_hat(struct MarioState *m) {
    const u32 source = m->action;
    controller.buttonPressed = HAT_BUTTON;
    mario_hat_update(m);
    assert(m->action == ACT_IOS_HAT_THROW);
    mario_hat_execute_throw(m);
    assert(m->action == source);
    controller.buttonPressed = 0;
}

static bool bounce_from_above(struct MarioState *m, u32 action) {
    m->action = action;
    m->pos[0] = hatObject.oPosX - 5.0f;
    m->pos[1] = hatObject.oPosY + 25.0f;
    m->pos[2] = hatObject.oPosZ;
    m->vel[0] = 20.0f;
    m->vel[1] = -32.0f;
    m->vel[2] = -4.0f;
    assert(perform_air_step(m, 0) == AIR_STEP_NONE);
    assert(m->vel[0] == 20.0f && m->vel[2] == -4.0f);
    return m->vel[1] == IOS_HAT_BOUNCE_IMPULSE;
}

static void land_on_floor(struct MarioState *m) {
    m->pos[1] = m->floorHeight + 3.0f;
    m->vel[0] = m->vel[2] = 0.0f;
    m->vel[1] = -20.0f;
    assert(perform_air_step(m, 0) == AIR_STEP_LANDED);
}

static void test_gameplay(void) {
    struct MarioState *m = fixture();
    m->actionTimer = 9;
    m->prevAction = ACT_WALKING;
    m->input = INPUT_A_PRESSED | INPUT_B_PRESSED;
    throw_hat(m);
    assert(mario_hat_is_away() && hatObject.oPosY == 96.0f && hatObject.oPosZ > 0.0f);
    assert(m->actionTimer == 9 && m->prevAction == ACT_WALKING);
    assert(m->input == (INPUT_A_PRESSED | INPUT_B_PRESSED));
    assert(m->flags == (MARIO_NORMAL_CAP | MARIO_CAP_ON_HEAD));
    assert(!bounce_from_above(m, ACT_JUMP)); // A cap still inside the thrower's reach cannot bounce.
    for (int i = 0; i < 16; ++i) mario_hat_update(m);
    obstructed = true;
    assert(!bounce_from_above(m, ACT_JUMP)); // The cap is within range but behind a surface.
    obstructed = false;
    assert(bounce_from_above(m, ACT_JUMP));
    assert(m->action == ACT_JUMP && m->peakHeight == m->pos[1]);
    assert(bounces == 1);
    assert(!bounce_from_above(m, ACT_JUMP)); // Already returning.
    for (int i = 0; i < 32; ++i) mario_hat_update(m);
    assert(!mario_hat_is_away() && hatObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);

    m->pos[1] = 225.0f;
    m->intendedMag = 20.0f;
    m->intendedYaw = 0x4000;
    m->vel[1] = -4.0f;
    const f32 startX = m->pos[0];
    throw_hat(m);
    assert(mario_hat_is_away() && hatObject.oPosY == 149.0f && hatObject.oPosX > startX);
    assert(m->vel[1] == -4.0f);
    for (int i = 0; i < 16; ++i) mario_hat_update(m);
    assert(!bounce_from_above(m, ACT_JUMP)); // No extra bounce after a midair rethrow.
    for (int i = 0; i < 64; ++i) mario_hat_update(m);
    assert(!mario_hat_is_away() && throws == 2 && bounces == 1);

    land_on_floor(m);
    m->action = ACT_JUMP_LAND;
    throw_hat(m);
    for (int i = 0; i < 16; ++i) mario_hat_update(m);
    assert(bounce_from_above(m, ACT_JUMP) && bounces == 2);
}

static void test_rapid_jump(u32 landingAction, u32 nextAirAction, bool missingFloor) {
    struct MarioState *m = fixture();
    throw_hat(m);
    for (int i = 0; i < 16; ++i) mario_hat_update(m);
    assert(bounce_from_above(m, ACT_JUMP));
    for (int i = 0; i < 32; ++i) mario_hat_update(m);

    floorMissing = missingFloor;
    land_on_floor(m);
    floorMissing = false;
    // Model a landing handler that immediately takes the next jump input.
    m->action = landingAction;
    m->input = INPUT_A_PRESSED | INPUT_Z_DOWN;
    mario_hat_update(m);
    assert(m->action == landingAction);
    m->action = nextAirAction;
    m->input = INPUT_A_DOWN;
    m->vel[1] = 48.0f;
    for (int i = 0; i < 5; ++i) {
        assert(perform_air_step(m, 0) == AIR_STEP_NONE);
    }
    throw_hat(m);
    for (int i = 0; i < 16; ++i) mario_hat_update(m);
    assert(bounce_from_above(m, nextAirAction) == !missingFloor);
}

static void test_crossing_paths(bool shouldBounce) {
    struct MarioState *m = fixture();
    m->action = ACT_WALKING;
    m->forwardVel = 32.0f;
    throw_hat(m);
    for (int i = 0; i < 4; ++i) mario_hat_update(m);
    const f32 hatStartZ = hatObject.oPosZ;
    mario_hat_update(m);
    const f32 hatEndZ = hatObject.oPosZ;

    // The feet cross the cap plane five eighths into the tick, in the third
    // quarter step. Mario moves against the cap's outward direction.
    const f32 contactZ = hatStartZ + (hatEndZ - hatStartZ) * 0.625f;
    const f32 separation = shouldBounce ? -(IOS_HAT_BOUNCE_RADIUS - 4.0f)
                                        : IOS_HAT_BOUNCE_RADIUS + 4.0f;
    m->action = ACT_JUMP;
    m->pos[1] = hatObject.oPosY + 40.0f;
    m->pos[2] = contactZ + separation + 25.0f; // 40 units/tick times 5/8.
    m->vel[1] = -32.0f;
    m->vel[2] = -40.0f;

    // Using only the final cap position gives the opposite result.
    assert((fabsf(contactZ + separation - hatEndZ) <= IOS_HAT_BOUNCE_RADIUS) != shouldBounce);
    assert(perform_air_step(m, 0) == AIR_STEP_NONE);
    assert(bounces == (shouldBounce ? 1u : 0u));
    assert(m->vel[2] == -40.0f);
}

// Exercise the normal held-A jump and air steering at two approach speeds.
void update_air_without_turn(struct MarioState *m);

static void test_jump_onto_hat(f32 runSpeed) {
    struct MarioState *m = fixture();
    m->action = runSpeed > 0.0f ? ACT_WALKING : ACT_IDLE;
    m->forwardVel = runSpeed;
    m->intendedMag = 32.0f;
    throw_hat(m);

    m->action = ACT_JUMP;
    m->flags |= MARIO_UNKNOWN_08;
    m->input = INPUT_A_DOWN | INPUT_NONZERO_ANALOG;
    // ACT_JUMP launch values in set_mario_action_airborne.
    m->vel[1] = 42.0f + 0.25f * runSpeed;
    m->forwardVel *= 0.8f;
    for (int tick = 0; tick < 40; ++tick) {
        if (tick > 0) mario_hat_update(m);
        update_air_without_turn(m);
        if (perform_air_step(m, 0) == AIR_STEP_LANDED || bounces > 0) break;
    }
    assert(bounces == 1 && m->vel[1] == IOS_HAT_BOUNCE_IMPULSE);

    bool landed = false;
    for (int tick = 0; tick < 50 && !landed; ++tick) {
        mario_hat_update(m);
        update_air_without_turn(m);
        landed = perform_air_step(m, 0) == AIR_STEP_LANDED;
    }
    assert(landed && !mario_hat_is_away() && bounces == 1);
}

static void test_air_throw_into_bounce(void) {
    struct MarioState *m = fixture();
    m->action = ACT_JUMP;
    m->pos[1] = 250.0f;
    m->forwardVel = 25.0f;
    m->vel[1] = -4.0f;
    m->intendedMag = 32.0f;
    m->input = INPUT_NONZERO_ANALOG;
    throw_hat(m);
    for (int tick = 0; tick < 12 && bounces == 0; ++tick) {
        if (tick > 0) mario_hat_update(m);
        update_air_without_turn(m);
        if (perform_air_step(m, 0) == AIR_STEP_LANDED) break;
    }
    assert(bounces == 1 && m->vel[1] == IOS_HAT_BOUNCE_IMPULSE);
}

static void test_recovery(void) {
    struct MarioState *m = fixture();
    poolFull = true;
    throw_hat(m);
    assert(!mario_hat_is_away() && m->action == ACT_IDLE && throws == 0);
    poolFull = false;

    m->flags |= MARIO_WING_CAP;
    controller.buttonPressed = HAT_BUTTON;
    mario_hat_update(m);
    assert(m->action == ACT_IDLE && !mario_hat_is_away() && throws == 0);
    m->flags &= ~MARIO_WING_CAP;
    throw_hat(m);
    m->health = 0xFF;
    mario_hat_finish_frame(m);
    assert(!mario_hat_is_away() && hatObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);

    m->health = 0x880;
    throw_hat(m);
    body.capState = MARIO_HAS_DEFAULT_CAP_OFF;
    initiate_warp(gCurrLevelNum, gCurrentArea->index, 1, 0);
    assert(!mario_hat_is_away() && hatObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
    assert(body.capState == MARIO_HAS_DEFAULT_CAP_ON);
    mario_hat_update(m);
    assert(!mario_hat_is_away());
    sWarpDest.type = WARP_TYPE_NOT_WARPING;

    throw_hat(m);
    clear_areas();
    assert(!mario_hat_is_away() && hatObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
    assert(gCurrentArea == NULL);

    m = fixture();
    obstructed = true;
    throw_hat(m);
    mario_hat_update(m);
    assert(!mario_hat_is_away() && hatObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
}

static void test_trajectory(void) {
    struct IOSHat hat = { 0 };
    const float origin[3] = { 0, 80, 0 };
    const float target[3] = { 0, 120, 0 };

    assert(!ios_hat_launch(&hat, origin, NAN, 0.0f, 0.0f));
    assert(ios_hat_launch(&hat, origin, 0.0f, 1.0f, 0.0f));
    assert(!ios_hat_launch(&hat, origin, 1.0f, 0.0f, 0.0f));
    ios_hat_step(&hat, target, NULL);
    const float first = hat.pos[2];
    ios_hat_step(&hat, target, NULL);
    assert(hat.pos[2] > first && hat.pos[2] - first < first);

    for (int tick = 0; tick < 20; ++tick) ios_hat_step(&hat, target, NULL);
    assert(hat.phase == IOS_HAT_THROWN && hat.pos[2] > 250.0f);
    const float edge = IOS_HAT_BOUNCE_RADIUS + 1.0f;
    const float outsideStart[3] = { edge, 125.0f, hat.pos[2] };
    const float outsideEnd[3] = { edge, 75.0f, hat.pos[2] };
    const float insideStart[3] = { 0.0f, 125.0f, hat.pos[2] };
    const float insideEnd[3] = { 0.0f, 75.0f, hat.pos[2] };
    float velocity = -65.0f;
    assert(!ios_hat_bounce(&hat, outsideStart, outsideEnd, 0.0f, 1.0f, &velocity, NULL));
    assert(!ios_hat_bounce(&hat, insideEnd, insideStart, 0.0f, 1.0f, &velocity, NULL));
    assert(velocity == -65.0f);

    for (int tick = 0; tick < 18; ++tick) ios_hat_step(&hat, target, NULL);
    assert(hat.phase == IOS_HAT_RETURNING);
    const float farAway[3] = { 10000.0f, 120.0f, 0.0f };
    for (int tick = 0; tick < 30; ++tick) ios_hat_step(&hat, farAway, NULL);
    assert(hat.phase == IOS_HAT_ATTACHED); // Cannot lose it even if Mario outruns it.
}

static void test_cheat_hat_lifecycle(void) {
    for (int returning = 0; returning < 2; ++returning) {
        struct MarioState *m = fixture();
        cheats_playground_set(3, CHEAT_HAT_ON);
        playground_hat(m);
        throw_hat(m);
        assert(mario_hat_is_away());
        if (returning) {
            controller.buttonPressed = 0;
            for (int tick=0; tick<35 && mario_hat_is_away(); ++tick) mario_hat_update(m);
        }
        cheats_playground_set(3, CHEAT_HAT_OFF);
        playground_hat(m);
        assert(!mario_hat_is_away() && !(m->flags & MARIO_CAP_ON_HEAD));
        assert(hatObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
        savedCapFlags = SAVE_FLAG_CAP_ON_KLEPTO;
        cheats_playground_set(3, CHEAT_HAT_ON);
        playground_hat(m);
        assert(!savedCapFlags && (m->flags & MARIO_CAP_ON_HEAD));
        m->health = 0xff;
        mario_hat_finish_frame(m);
        assert(!mario_hat_is_away());
        m->health = 0x880;
        unload_area();
        assert(!mario_hat_is_away());
        m = fixture();
        playground_hat(m);
        assert(m->flags & MARIO_CAP_ON_HEAD);
    }
    cheats_playground_set(3, 0);
}

int main(void) {
    test_trajectory();
    test_gameplay();
    test_rapid_jump(ACT_BACKFLIP_LAND, ACT_BACKFLIP, false);
    test_rapid_jump(ACT_SIDE_FLIP_LAND, ACT_DOUBLE_JUMP, false);
    test_rapid_jump(ACT_LONG_JUMP_LAND, ACT_LONG_JUMP, false);
    test_rapid_jump(ACT_LONG_JUMP_LAND, ACT_LONG_JUMP, true); // Out-of-bounds fallback.
    test_crossing_paths(true);
    test_crossing_paths(false);
    test_jump_onto_hat(0.0f);
    test_jump_onto_hat(32.0f);
    test_air_throw_into_bounce();
    test_recovery();
    test_cheat_hat_lifecycle();
    puts("Hat movement, air step, and recovery checks passed");
    return 0;
}
