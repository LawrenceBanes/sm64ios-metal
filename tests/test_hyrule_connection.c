#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/mario.h"

#include "sm64.h"
#include "level_misc_macros.h"
#include "macro_preset_names.h"
#include "special_preset_names.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "game/mario_step.h"
#include "pc/cheats.h"
#include "build/hyrule_field_connection.h"
#ifdef VALIDATE_IMPORTED_GATE
#include "build/hyrule_field_spawn.h"
#endif
#include "game/object_list_processor.h"

// Compare the real terrain streams byte for byte up to the legacy end marker.
#undef IOS_CONNECTED_WORLD
#define castle_grounds_seg7_collision_level legacy_castle_collision
#include "levels/castle_grounds/areas/1/collision.inc.c"
#undef castle_grounds_seg7_collision_level
#define IOS_CONNECTED_WORLD 1
#include "levels/castle_grounds/areas/1/collision.inc.c"
#if defined(IOS_CONNECTED_WORLD_TEST)
#include "engine/world_region.h"
void world_region_reset(struct WorldPoint origin) { (void)origin; }
#endif

struct CheatList Cheats;

s32 gSurfacesAllocated;
u32 gGlobalTimer;
s32 gSurfaceNodesAllocated;
s32 gNumStaticSurfaces;
s32 gNumStaticSurfaceNodes;
u32 gTimeStopState;
s16 gCCMEnteredSlide;
s32 gNumFindFloorMisses;
struct NumTimesCalled gNumCalls;
s16 *gEnvironmentRegions;
s32 gEnvironmentLevels[20];
s16 gFindFloorIncludeSurfaceIntangible;
s16 gCheckingSurfaceCollisionsForCamera;
s16 gCurrLevelNum;
struct Object *gMarioObject;
struct Object *gCurrentObject;
struct MarioState *gMarioState;

void *main_pool_alloc(u32 bytes, u32 side) {
    (void)side;
    void *memory = calloc(1, bytes);
    assert(memory != NULL);
    return memory;
}

void update_walking_speed(struct MarioState *m);

void reset_red_coins_collected(void) {}
void spawn_macro_objects(s16 i, s16 *data) { (void)i; (void)data; }
void spawn_macro_objects_hardcoded(s16 i, s16 *data) { (void)i; (void)data; }
void spawn_special_objects(s16 i, s16 **data) {
    (void)i;
    assert(**data == 30);
    // Skip Castle's object records in this physics harness. Preserve the
    // stock water boxes and every collision bank.
    const s16 *end = castle_grounds_seg7_collision_level
        + sizeof(castle_grounds_seg7_collision_level) / sizeof(s16);
    while (*data + 4 < end) {
        if ((*data)[0] == TERRAIN_LOAD_ENVIRONMENT && (*data)[1] == 2
            && (*data)[2] == 0 && (*data)[3] == -7129) return;
        ++*data;
    }
    abort();
}

s32 __wrap_mario_ios_portal_surface_passable(const struct Surface *s, f32 x, f32 y, f32 z, f32 radius) {
    (void)s; (void)x; (void)y; (void)z; (void)radius;
    return 0;
}
u32 __wrap_mario_get_terrain_sound_addend(struct MarioState *m) { (void)m; return 0; }

static void start_mario(struct MarioState *state, f32 x, f32 lookupY, f32 z) {
    static struct Object mario;
    static struct Area area = { .index = 1, .terrainType = TERRAIN_GRASS };
    *state = (struct MarioState) { .action = ACT_WALKING, .marioObj = &mario, .area = &area };
    gMarioState = state;
    gMarioObject = gCurrentObject = &mario;
    state->pos[0] = x;
    state->pos[2] = z;
    state->floorHeight = find_floor(x, lookupY, z, &state->floor);
    assert(state->floor != NULL);
    state->pos[1] = state->floorHeight;
}

static void walk(f32 startX, f32 endX, f32 z, f32 lookupY) {
    struct MarioState state;
    start_mario(&state, startX, lookupY, z);
    state.faceAngle[1] = endX < startX ? -0x4000 : 0x4000;
    state.intendedYaw = state.faceAngle[1];
    state.intendedMag = 32.0f;
    state.forwardVel = 16.0f;
    for (int i = 0; i < 850; ++i) {
        const f32 before = state.pos[0];
        const f32 beforeY = state.pos[1];
        state.floorAngle = atan2s(state.floor->normal.z, state.floor->normal.x);
        assert(!mario_floor_is_slippery(&state));
        update_walking_speed(&state);
        const s32 step = perform_ground_step(&state);
        if (step != GROUND_STEP_NONE || fabsf(state.pos[1] - beforeY) > 32.0f
            || (state.pos[0] < -12050.0f && fabsf(state.pos[1] - 260.0f) > 8.0f)
            || (state.pos[0] - before) * state.vel[0] <= 0.0f) {
            fprintf(stderr, "blocked: %g -> %g, pos=(%g,%g,%g) step=%d\n",
                    startX, endX, state.pos[0], state.pos[1], state.pos[2], step);
            fprintf(stderr, "floor=%g surface=%d x1=%d y1=%d z1=%d\n", state.floorHeight, state.floor->type, state.floor->vertex1[0], state.floor->vertex1[1], state.floor->vertex1[2]);
            abort();
        }
        if ((state.pos[0] - endX) * state.vel[0] >= 0.0f) return;
    }
    abort();
}

static void push_into_rail(f32 x, f32 deckY, f32 halfWidth, int side) {
    const f32 centerZ = HYRULE_CONNECTION_Z;
    struct MarioState state;
    start_mario(&state, x, deckY + 16.0f, centerZ + side * (halfWidth - 70.0f));
    state.faceAngle[1] = side < 0 ? (s16)0x8000 : 0;
    state.vel[2] = 16.0f * side;
    bool hitRail = false;
    for (int i = 0; i < 16; ++i) {
        const s32 step = perform_ground_step(&state);
        assert(step == GROUND_STEP_NONE || step == GROUND_STEP_HIT_WALL);
        assert(fabsf(state.pos[1] - deckY) < 2.0f);
        assert((state.pos[2] - centerZ) * side < halfWidth - 24.0f);
        hitRail |= step == GROUND_STEP_HIT_WALL;
    }
    assert(hitRail);
}

static void jump_across_crest(int direction, int side) {
    struct MarioState state;
    const f32 startX = direction < 0 ? -7940.0f : -8410.0f;
    const f32 z = HYRULE_CONNECTION_Z + side * 650.0f;
    start_mario(&state, startX, 1850.0f, z);
    assert(state.pos[1] > 1700.0f);
    state.action = ACT_JUMP;
    state.faceAngle[1] = direction < 0 ? -0x4000 : 0x4000;
    state.input = INPUT_A_DOWN;
    state.vel[0] = 19.0f * direction;
    state.vel[1] = 42.0f;
    bool landed = false;
    f32 peak = state.pos[1];

    for (int i = 0; i < 48 && !landed; ++i) {
        const s32 step = perform_air_step(&state, 0);
        assert(step == AIR_STEP_NONE || step == AIR_STEP_LANDED);
        assert(fabsf(state.pos[2] - z) < 2.0f);
        assert(state.pos[1] > 1600.0f);
        peak = fmaxf(peak, state.pos[1]);
        landed = step == AIR_STEP_LANDED;
    }
    assert(landed && peak > 1900.0f);
    assert(fabsf(state.pos[1] - state.floorHeight) < 0.01f);
    assert(direction < 0 ? state.pos[0] < -8300.0f : state.pos[0] > -8060.0f);
    state.action = ACT_WALKING;
    assert(perform_ground_step(&state) == GROUND_STEP_NONE);
}

static void check_frame_transform(void) {
    Mat4 matrix;
    Vec3f origin = { HYRULE_CONNECTION_X, HYRULE_CONNECTION_Y, HYRULE_CONNECTION_Z };
    Vec3s rotation = { 0, HYRULE_CONNECTION_YAW * 0x8000 / 180, 0 };
    mtxf_rotate_zxy_and_translate(matrix, origin, rotation);
    assert(fabsf(matrix[2][0] + 1.0f) < 0.001f && fabsf(matrix[0][2] - 1.0f) < 0.001f);
    assert(matrix[3][0] == HYRULE_CONNECTION_X && matrix[3][1] == HYRULE_CONNECTION_Y
           && matrix[3][2] == HYRULE_CONNECTION_Z);
}

int main(void) {
    const size_t prefix = sizeof(legacy_castle_collision) - sizeof(s16);
    assert(legacy_castle_collision[prefix / sizeof(s16)] == TERRAIN_LOAD_END);
    assert(memcmp(legacy_castle_collision, castle_grounds_seg7_collision_level, prefix) == 0);
    check_frame_transform();
    alloc_surface_pools();
    load_area_terrain(1, (s16 *)castle_grounds_seg7_collision_level, NULL, NULL);
    assert(gSurfaceLoadRejectedCount == 0);
    assert(gNumStaticSurfaces < 6500 && gNumStaticSurfaceNodes < 24576);
    printf("Static collision: %d surfaces, %d nodes; no rejects (%zu/%zu bytes each)\n",
           gNumStaticSurfaces, gNumStaticSurfaceNodes, sizeof(struct Surface), sizeof(struct SurfaceNode));
    for (int offset = -92; offset <= 92; offset += 92) {
        walk(-4400, -14600, HYRULE_CONNECTION_Z + offset, 270.0f);
        walk(-14600, -4400, HYRULE_CONNECTION_Z + offset, 270.0f);
    }
    for (int side = -1; side <= 1; side += 2) {
        const f32 edgeZ = HYRULE_CONNECTION_Z + side * 686.0f;
        walk(-8070, -8275, edgeZ, 1840.0f);
        walk(-8275, -8070, edgeZ, 1840.0f);
        push_into_rail(-8180.0f, 1830.0f, 740.0f, side);
        push_into_rail(HYRULE_CONNECTION_X - 900.0f, 260.0f, 144.0f, side);
        jump_across_crest(-1, side);
        jump_across_crest(1, side);
    }
    puts("Castle and field inlet: both directions, both walkway edges, rail contacts, and crest jump landings passed");
    gMarioState = NULL;
    gCurrentObject = gMarioObject = NULL;
#ifdef VALIDATE_IMPORTED_GATE
    struct Surface *gateFloor;
    const f32 gateY = HYRULE_CONNECTION_Y + HYRULE_FIELD_GATE_Y;
    const f32 floorY = find_floor(HYRULE_CONNECTION_X - HYRULE_FIELD_GATE_Z, gateY + 16.0f,
                                 HYRULE_CONNECTION_Z + HYRULE_FIELD_GATE_X, &gateFloor);
    assert(gateFloor != NULL && fabsf(floorY - gateY) < 4.0f);
    printf("Transformed gate anchor has a registered floor at %.2f\n", floorY);
#endif
    free(sSurfacePool);
    free(sSurfaceNodePool);
    return 0;
}
