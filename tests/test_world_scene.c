#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "engine/world_scene.h"

struct TestContext {
    u32 failLoadScene;
    u32 failActivateRoom;
    u32 loads;
    u32 releases;
    u32 active[3][64];
};

static const u32 room10Neighbors[] = {20};
static const u32 room20Neighbors[] = {10};
static const struct WorldSceneRoomDesc scene1Rooms[] = {
    {10, {0, 0, 0}, {1000, 500, 1000}, room10Neighbors, 1, NULL},
    {20, {1001, 0, 0}, {2000, 500, 1000}, room20Neighbors, 1, NULL},
    {30, {3000, 0, 0}, {4000, 500, 1000}, NULL, 0, NULL},
};
static const struct WorldSceneEntranceDesc scene1Entrances[] = {
    {100, 10, {100, 0, 100}, 0},
    {101, 20, {1500, 0, 100}, 0},
};
static const struct WorldSceneExitDesc scene1Exits[] = {
    {55, 2, 200},
};
static const struct WorldSceneRoomDesc scene2Rooms[] = {
    {40, {-1000, 0, -1000}, {1000, 800, 1000}, NULL, 0, NULL},
};
static const struct WorldSceneEntranceDesc scene2Entrances[] = {
    {200, 40, {0, 100, 0}, 0x4000},
};
static const struct WorldSceneExitDesc scene2Exits[] = {
    {66, 1, 100},
};
static const u32 invalidNeighbor[] = {99};
static const struct WorldSceneRoomDesc malformedRooms[] = {
    {50, {0, 0, 0}, {10, 10, 10}, invalidNeighbor, 1, NULL},
};
static const struct WorldSceneEntranceDesc malformedEntrances[] = {
    {300, 50, {0, 0, 0}, 0},
};

static s32 load_scene(void *context, u32 id, struct WorldScenePackage *out) {
    struct TestContext *test = context;
    test->loads++;
    if (test->failLoadScene == id) return 0;
    memset(out, 0, sizeof(*out));
    out->id = id;
    if (id == 1) {
        out->rooms = scene1Rooms; out->roomCount = 3;
        out->entrances = scene1Entrances; out->entranceCount = 2;
        out->exits = scene1Exits; out->exitCount = 1;
    } else if (id == 2) {
        out->rooms = scene2Rooms; out->roomCount = 1;
        out->entrances = scene2Entrances; out->entranceCount = 1;
        out->exits = scene2Exits; out->exitCount = 1;
    } else if (id == 3) {
        out->rooms = malformedRooms; out->roomCount = 1;
        out->entrances = malformedEntrances; out->entranceCount = 1;
    } else {
        return 0;
    }
    return 1;
}

static void release_scene(void *context, struct WorldScenePackage *package) {
    struct TestContext *test = context;
    (void)package;
    test->releases++;
}

static s32 activate_room(void *context, const struct WorldScenePackage *package,
                         const struct WorldSceneRoomDesc *room) {
    struct TestContext *test = context;
    if (test->failActivateRoom == room->id) return 0;
    assert(package->id < 3 && room->id < 64);
    assert(test->active[package->id][room->id] == 0);
    test->active[package->id][room->id] = 1;
    return 1;
}

static void deactivate_room(void *context, const struct WorldScenePackage *package,
                            const struct WorldSceneRoomDesc *room) {
    struct TestContext *test = context;
    assert(package->id < 3 && room->id < 64);
    assert(test->active[package->id][room->id] == 1);
    test->active[package->id][room->id] = 0;
}

static void test_stream_and_transactional_transition(void) {
    struct TestContext context = {0};
    const struct WorldSceneProvider provider = {
        load_scene, release_scene, activate_room, deactivate_room
    };
    struct WorldSceneRuntime runtime;
    struct WorldSceneDestination destination;
    struct WorldPoint player = {900, 100, 100};
    world_scene_runtime_init(&runtime, &provider, &context);
    assert(world_scene_open(&runtime, 1, 100, &destination) == WORLD_SCENE_OK);
    assert(destination.sceneId == 1 && destination.roomId == 10
           && destination.position.x == 100);
    assert(world_scene_active_room_count(&runtime) == 1);
    assert(world_scene_update_residency(&runtime, player, 100, 0) == WORLD_SCENE_OK);
    assert(world_scene_active_room_count(&runtime) == 2);
    assert(context.active[1][10] && context.active[1][20]);
    player.x = 1500;
    assert(world_scene_update_residency(&runtime, player, 100, 0) == WORLD_SCENE_OK);
    assert(world_scene_current_room(&runtime) == 20);

    /* Failed room activation rolls back only newly attempted residency. */
    context.failActivateRoom = 30;
    player.x = 3500;
    assert(world_scene_update_residency(&runtime, player, 0, 0)
           == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_active_id(&runtime) == 1);
    assert(world_scene_active_room_count(&runtime) == 2);
    assert(!context.active[1][30]);
    context.failActivateRoom = 0;

    /* A failed destination load leaves the current scene and room intact. */
    context.failLoadScene = 2;
    assert(world_scene_traverse_exit(&runtime, 55, &destination)
           == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_active_id(&runtime) == 1);
    assert(world_scene_current_room(&runtime) == 20);
    assert(context.active[1][20]);
    context.failLoadScene = 0;

    assert(world_scene_traverse_exit(&runtime, 55, &destination) == WORLD_SCENE_OK);
    assert(destination.sceneId == 2 && destination.roomId == 40
           && destination.entranceId == 200 && destination.yaw == 0x4000);
    assert(world_scene_active_id(&runtime) == 2);
    assert(world_scene_active_room_count(&runtime) == 1);
    assert(!context.active[1][10] && !context.active[1][20]);
    assert(context.active[2][40]);

    assert(world_scene_traverse_exit(&runtime, 66, &destination) == WORLD_SCENE_OK);
    assert(destination.sceneId == 1 && destination.roomId == 10);
    assert(world_scene_active_id(&runtime) == 1);
    world_scene_close(&runtime);
    assert(world_scene_active_id(&runtime) == 0);
    assert(context.active[1][10] == 0 && context.active[2][40] == 0);
    assert(context.loads == 4 && context.releases == 3);
}

static void test_invalid_package_and_same_scene_switch(void) {
    struct TestContext context = {0};
    const struct WorldSceneProvider provider = {
        load_scene, release_scene, activate_room, deactivate_room
    };
    struct WorldSceneRuntime runtime;
    struct WorldSceneDestination destination;
    world_scene_runtime_init(&runtime, &provider, &context);
    assert(world_scene_open(&runtime, 3, 300, &destination) == WORLD_SCENE_BAD_PACKAGE);
    assert(world_scene_active_id(&runtime) == 0 && context.releases == 1);
    assert(world_scene_open(&runtime, 1, 999, &destination) == WORLD_SCENE_NOT_FOUND);
    assert(world_scene_active_id(&runtime) == 0 && context.releases == 2);
    assert(world_scene_open(&runtime, 1, 100, &destination) == WORLD_SCENE_OK);
    assert(world_scene_transition(&runtime, 1, 101, &destination) == WORLD_SCENE_OK);
    assert(destination.roomId == 20 && world_scene_current_room(&runtime) == 20);
    assert(context.active[1][10] == 0 && context.active[1][20] == 1);
    assert(world_scene_update_residency(&runtime, (struct WorldPoint){9000, 0, 0}, 0, 0)
           == WORLD_SCENE_PLAYER_OUTSIDE);
    assert(world_scene_active_room_count(&runtime) == 1);
    world_scene_close(&runtime);
}

int main(void) {
    test_stream_and_transactional_transition();
    test_invalid_package_and_same_scene_switch();
    puts("world scene package lifecycle: passed");
    return 0;
}
