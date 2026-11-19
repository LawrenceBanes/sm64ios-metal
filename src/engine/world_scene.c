#include "world_scene.h"

#if defined(IOS_O2R_SCENE_RUNTIME)

#include <stdlib.h>
#include <string.h>

static s32 same_point_in_bounds(struct WorldPoint point,
                               const struct WorldSceneRoomDesc *room) {
    return point.x >= room->min.x && point.x <= room->max.x
        && point.y >= room->min.y && point.y <= room->max.y
        && point.z >= room->min.z && point.z <= room->max.z;
}

static u32 room_index(const struct WorldScenePackage *package, u32 id) {
    u32 i;
    for (i = 0; i < package->roomCount; i++) {
        if (package->rooms[i].id == id) return i;
    }
    return package->roomCount;
}

static const struct WorldSceneEntranceDesc *find_entrance(
        const struct WorldScenePackage *package, u32 id) {
    u32 i;
    for (i = 0; i < package->entranceCount; i++) {
        if (package->entrances[i].id == id) return &package->entrances[i];
    }
    return NULL;
}

static enum WorldSceneResult validate_package(const struct WorldScenePackage *package) {
    u32 i, j;
    if (package == NULL || package->id == 0 || package->rooms == NULL
        || package->roomCount == 0 || package->roomCount > WORLD_SCENE_MAX_ROOMS
        || package->entranceCount > WORLD_SCENE_MAX_ENTRANCES
        || package->exitCount > WORLD_SCENE_MAX_EXITS
        || (package->entranceCount != 0 && package->entrances == NULL)
        || (package->exitCount != 0 && package->exits == NULL)) {
        return WORLD_SCENE_BAD_PACKAGE;
    }
    for (i = 0; i < package->roomCount; i++) {
        const struct WorldSceneRoomDesc *room = &package->rooms[i];
        if (room->id == 0 || room->min.x > room->max.x || room->min.y > room->max.y
            || room->min.z > room->max.z
            || room->neighborCount > WORLD_SCENE_MAX_ROOM_NEIGHBORS
            || (room->neighborCount != 0 && room->neighbors == NULL)) {
            return WORLD_SCENE_BAD_PACKAGE;
        }
        for (j = 0; j < i; j++) {
            if (package->rooms[j].id == room->id) return WORLD_SCENE_BAD_PACKAGE;
        }
        for (j = 0; j < room->neighborCount; j++) {
            u32 k;
            if (room->neighbors[j] == room->id
                || room_index(package, room->neighbors[j]) == package->roomCount) {
                return WORLD_SCENE_BAD_PACKAGE;
            }
            for (k = 0; k < j; k++) {
                if (room->neighbors[k] == room->neighbors[j]) return WORLD_SCENE_BAD_PACKAGE;
            }
        }
    }
    for (i = 0; i < package->entranceCount; i++) {
        u32 entranceRoom = room_index(package, package->entrances[i].roomId);
        if (entranceRoom == package->roomCount
            || !same_point_in_bounds(package->entrances[i].position,
                                     &package->rooms[entranceRoom])) {
            return WORLD_SCENE_BAD_PACKAGE;
        }
        for (j = 0; j < i; j++) {
            if (package->entrances[j].id == package->entrances[i].id) {
                return WORLD_SCENE_BAD_PACKAGE;
            }
        }
    }
    for (i = 0; i < package->exitCount; i++) {
        if (package->exits[i].destinationSceneId == 0) return WORLD_SCENE_BAD_PACKAGE;
        for (j = 0; j < i; j++) {
            if (package->exits[j].id == package->exits[i].id) return WORLD_SCENE_BAD_PACKAGE;
        }
    }
    return WORLD_SCENE_OK;
}

static void destination_from_entrance(const struct WorldScenePackage *package,
        const struct WorldSceneEntranceDesc *entrance,
        struct WorldSceneDestination *destination) {
    if (destination == NULL) return;
    destination->sceneId = package->id;
    destination->roomId = entrance->roomId;
    destination->entranceId = entrance->id;
    destination->position = entrance->position;
    destination->yaw = entrance->yaw;
}

void world_scene_runtime_init(struct WorldSceneRuntime *runtime,
                              const struct WorldSceneProvider *provider,
                              void *providerContext) {
    if (runtime == NULL) return;
    memset(runtime, 0, sizeof(*runtime));
    if (provider != NULL) runtime->provider = *provider;
    runtime->providerContext = providerContext;
}

static void deactivate_all(struct WorldSceneRuntime *runtime,
                           struct WorldScenePackage *package,
                           const u8 *activeRooms) {
    u32 i;
    if (activeRooms == NULL) return;
    for (i = 0; i < package->roomCount; i++) {
        if (activeRooms[i]) {
            runtime->provider.deactivateRoom(runtime->providerContext, package,
                                             &package->rooms[i]);
        }
    }
}

void world_scene_close(struct WorldSceneRuntime *runtime) {
    if (runtime == NULL || !runtime->isOpen) return;
    deactivate_all(runtime, &runtime->activePackage, runtime->activeRooms);
    free(runtime->activeRooms);
    runtime->activeRooms = NULL;
    runtime->provider.releaseScene(runtime->providerContext, &runtime->activePackage);
    memset(&runtime->activePackage, 0, sizeof(runtime->activePackage));
    runtime->currentRoomId = 0;
    runtime->isOpen = 0;
}

static enum WorldSceneResult check_runtime(const struct WorldSceneRuntime *runtime) {
    if (runtime == NULL || runtime->provider.loadScene == NULL
        || runtime->provider.releaseScene == NULL
        || runtime->provider.activateRoom == NULL
        || runtime->provider.deactivateRoom == NULL) return WORLD_SCENE_BAD_ARGUMENT;
    return WORLD_SCENE_OK;
}

enum WorldSceneResult world_scene_open(struct WorldSceneRuntime *runtime,
                                       u32 sceneId, u32 entranceId,
                                       struct WorldSceneDestination *destination) {
    enum WorldSceneResult result = check_runtime(runtime);
    struct WorldScenePackage package;
    const struct WorldSceneEntranceDesc *entrance;
    u8 *activeRooms;
    u32 index;
    if (result != WORLD_SCENE_OK || sceneId == 0) return WORLD_SCENE_BAD_ARGUMENT;
    if (runtime->isOpen) return world_scene_transition(runtime, sceneId, entranceId, destination);
    memset(&package, 0, sizeof(package));
    if (!runtime->provider.loadScene(runtime->providerContext, sceneId, &package))
        return WORLD_SCENE_PROVIDER_FAILED;
    result = validate_package(&package);
    if (result == WORLD_SCENE_OK && package.id != sceneId) result = WORLD_SCENE_BAD_PACKAGE;
    entrance = result == WORLD_SCENE_OK ? find_entrance(&package, entranceId) : NULL;
    if (entrance == NULL) result = result == WORLD_SCENE_OK ? WORLD_SCENE_NOT_FOUND : result;
    index = entrance != NULL ? room_index(&package, entrance->roomId) : 0;
    activeRooms = result == WORLD_SCENE_OK ? calloc(package.roomCount, sizeof(*activeRooms)) : NULL;
    if (result == WORLD_SCENE_OK && activeRooms == NULL) result = WORLD_SCENE_NO_MEMORY;
    if (result == WORLD_SCENE_OK
        && !runtime->provider.activateRoom(runtime->providerContext, &package, &package.rooms[index])) {
        result = WORLD_SCENE_PROVIDER_FAILED;
    }
    if (result != WORLD_SCENE_OK) {
        free(activeRooms);
        runtime->provider.releaseScene(runtime->providerContext, &package);
        return result;
    }
    activeRooms[index] = 1;
    runtime->activePackage = package;
    runtime->activeRooms = activeRooms;
    runtime->currentRoomId = entrance->roomId;
    runtime->isOpen = 1;
    destination_from_entrance(&runtime->activePackage, entrance, destination);
    return WORLD_SCENE_OK;
}

enum WorldSceneResult world_scene_transition(struct WorldSceneRuntime *runtime,
                                             u32 sceneId, u32 entranceId,
                                             struct WorldSceneDestination *destination) {
    enum WorldSceneResult result = check_runtime(runtime);
    struct WorldScenePackage next;
    const struct WorldSceneEntranceDesc *entrance;
    u32 nextIndex;
    u8 *nextRooms;
    if (result != WORLD_SCENE_OK || !runtime->isOpen || sceneId == 0) {
        return result == WORLD_SCENE_OK ? WORLD_SCENE_NOT_OPEN : result;
    }
    if (sceneId == runtime->activePackage.id) {
        entrance = find_entrance(&runtime->activePackage, entranceId);
        if (entrance == NULL) return WORLD_SCENE_NOT_FOUND;
        nextIndex = room_index(&runtime->activePackage, entrance->roomId);
        if (nextIndex >= runtime->activePackage.roomCount) return WORLD_SCENE_BAD_PACKAGE;
        if (!runtime->activeRooms[nextIndex]
            && !runtime->provider.activateRoom(runtime->providerContext,
                    &runtime->activePackage, &runtime->activePackage.rooms[nextIndex])) {
            return WORLD_SCENE_PROVIDER_FAILED;
        }
        if (runtime->currentRoomId != entrance->roomId) {
            u32 oldIndex = room_index(&runtime->activePackage, runtime->currentRoomId);
            if (oldIndex < runtime->activePackage.roomCount && runtime->activeRooms[oldIndex]) {
                runtime->provider.deactivateRoom(runtime->providerContext,
                    &runtime->activePackage, &runtime->activePackage.rooms[oldIndex]);
                runtime->activeRooms[oldIndex] = 0;
            }
        }
        runtime->activeRooms[nextIndex] = 1;
        runtime->currentRoomId = entrance->roomId;
        destination_from_entrance(&runtime->activePackage, entrance, destination);
        return WORLD_SCENE_OK;
    }

    memset(&next, 0, sizeof(next));
    if (!runtime->provider.loadScene(runtime->providerContext, sceneId, &next))
        return WORLD_SCENE_PROVIDER_FAILED;
    result = validate_package(&next);
    if (result == WORLD_SCENE_OK && next.id != sceneId) result = WORLD_SCENE_BAD_PACKAGE;
    entrance = result == WORLD_SCENE_OK ? find_entrance(&next, entranceId) : NULL;
    if (entrance == NULL) result = result == WORLD_SCENE_OK ? WORLD_SCENE_NOT_FOUND : result;
    nextIndex = entrance != NULL ? room_index(&next, entrance->roomId) : 0;
    nextRooms = result == WORLD_SCENE_OK ? calloc(next.roomCount, sizeof(*nextRooms)) : NULL;
    if (result == WORLD_SCENE_OK && nextRooms == NULL) result = WORLD_SCENE_NO_MEMORY;
    if (result == WORLD_SCENE_OK
        && !runtime->provider.activateRoom(runtime->providerContext, &next, &next.rooms[nextIndex])) {
        result = WORLD_SCENE_PROVIDER_FAILED;
    }
    if (result != WORLD_SCENE_OK) {
        free(nextRooms);
        runtime->provider.releaseScene(runtime->providerContext, &next);
        return result;
    }
    nextRooms[nextIndex] = 1;
    deactivate_all(runtime, &runtime->activePackage, runtime->activeRooms);
    free(runtime->activeRooms);
    runtime->provider.releaseScene(runtime->providerContext, &runtime->activePackage);
    runtime->activePackage = next;
    runtime->activeRooms = nextRooms;
    runtime->currentRoomId = entrance->roomId;
    destination_from_entrance(&runtime->activePackage, entrance, destination);
    return WORLD_SCENE_OK;
}

enum WorldSceneResult world_scene_traverse_exit(struct WorldSceneRuntime *runtime,
                                                u32 exitId,
                                                struct WorldSceneDestination *destination) {
    u32 i;
    if (runtime == NULL || !runtime->isOpen) return WORLD_SCENE_NOT_OPEN;
    for (i = 0; i < runtime->activePackage.exitCount; i++) {
        const struct WorldSceneExitDesc *exit = &runtime->activePackage.exits[i];
        if (exit->id == exitId) {
            return world_scene_transition(runtime, exit->destinationSceneId,
                                          exit->destinationEntranceId, destination);
        }
    }
    return WORLD_SCENE_NOT_FOUND;
}

static u64 distance_outside(s64 value, s64 minimum, s64 maximum) {
    if (value < minimum) return (u64)minimum - (u64)value;
    if (value > maximum) return (u64)value - (u64)maximum;
    return 0;
}

static u32 saturating_add3(u32 a, u32 b, u32 c) {
    u64 result = (u64)a + b + c;
    return result > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)result;
}

enum WorldSceneResult world_scene_update_residency(struct WorldSceneRuntime *runtime,
                                                   struct WorldPoint player,
                                                   u32 maxStep,
                                                   u32 prefetchDistance) {
    u8 *wanted;
    u32 playerIndex = 0xFFFFFFFFu, i, j;
    u32 loadDistance = saturating_add3(prefetchDistance, maxStep, maxStep);
    u32 unloadDistance = saturating_add3(loadDistance, prefetchDistance, 1024);
    enum WorldSceneResult result = check_runtime(runtime);
    if (result != WORLD_SCENE_OK) return result;
    if (!runtime->isOpen) return WORLD_SCENE_NOT_OPEN;
    for (i = 0; i < runtime->activePackage.roomCount; i++) {
        if (same_point_in_bounds(player, &runtime->activePackage.rooms[i])) {
            playerIndex = i;
            if (runtime->activePackage.rooms[i].id == runtime->currentRoomId) break;
        }
    }
    if (playerIndex == 0xFFFFFFFFu) return WORLD_SCENE_PLAYER_OUTSIDE;
    wanted = calloc(runtime->activePackage.roomCount, sizeof(*wanted));
    if (wanted == NULL) return WORLD_SCENE_NO_MEMORY;
    for (i = 0; i < runtime->activePackage.roomCount; i++) {
        const struct WorldSceneRoomDesc *room = &runtime->activePackage.rooms[i];
        u64 dx = distance_outside(player.x, room->min.x, room->max.x);
        u64 dy = distance_outside(player.y, room->min.y, room->max.y);
        u64 dz = distance_outside(player.z, room->min.z, room->max.z);
        wanted[i] = dx <= loadDistance && dy <= loadDistance && dz <= loadDistance;
    }
    wanted[playerIndex] = 1;
    for (j = 0; j < runtime->activePackage.rooms[playerIndex].neighborCount; j++) {
        u32 neighbor = room_index(&runtime->activePackage,
            runtime->activePackage.rooms[playerIndex].neighbors[j]);
        if (neighbor < runtime->activePackage.roomCount) wanted[neighbor] = 1;
    }
    /* Activate first: a provider failure leaves the old active set unchanged. */
    for (i = 0; i < runtime->activePackage.roomCount; i++) {
        if (wanted[i] && !runtime->activeRooms[i]
            && !runtime->provider.activateRoom(runtime->providerContext,
                    &runtime->activePackage, &runtime->activePackage.rooms[i])) {
            for (j = 0; j < i; j++) {
                if (wanted[j] && !runtime->activeRooms[j]) {
                    runtime->provider.deactivateRoom(runtime->providerContext,
                        &runtime->activePackage, &runtime->activePackage.rooms[j]);
                }
            }
            free(wanted);
            return WORLD_SCENE_PROVIDER_FAILED;
        }
    }
    for (i = 0; i < runtime->activePackage.roomCount; i++) {
        const struct WorldSceneRoomDesc *room = &runtime->activePackage.rooms[i];
        u64 dx = distance_outside(player.x, room->min.x, room->max.x);
        u64 dy = distance_outside(player.y, room->min.y, room->max.y);
        u64 dz = distance_outside(player.z, room->min.z, room->max.z);
        s32 distant = dx > unloadDistance || dy > unloadDistance || dz > unloadDistance;
        if (runtime->activeRooms[i] && !wanted[i] && distant) {
            runtime->provider.deactivateRoom(runtime->providerContext,
                &runtime->activePackage, room);
            runtime->activeRooms[i] = 0;
        } else if (wanted[i]) {
            runtime->activeRooms[i] = 1;
        }
    }
    runtime->currentRoomId = runtime->activePackage.rooms[playerIndex].id;
    free(wanted);
    return WORLD_SCENE_OK;
}

void world_scene_track_room(struct WorldSceneRuntime *runtime, struct WorldPoint player) {
    if (runtime == NULL || !runtime->isOpen) return;
    const struct WorldScenePackage *package = &runtime->activePackage;
    u32 current = room_index(package, runtime->currentRoomId);
    if (current < package->roomCount && same_point_in_bounds(player, &package->rooms[current])) return;
    for (u32 i = 0; i < package->roomCount; ++i) {
        if (runtime->activeRooms[i] && same_point_in_bounds(player, &package->rooms[i])) {
            runtime->currentRoomId = package->rooms[i].id;
            return;
        }
    }
}

u32 world_scene_active_id(const struct WorldSceneRuntime *runtime) {
    return runtime != NULL && runtime->isOpen ? runtime->activePackage.id : 0;
}

u32 world_scene_current_room(const struct WorldSceneRuntime *runtime) {
    return runtime != NULL && runtime->isOpen ? runtime->currentRoomId : 0;
}

u32 world_scene_active_room_count(const struct WorldSceneRuntime *runtime) {
    u32 i, count = 0;
    if (runtime == NULL || !runtime->isOpen || runtime->activeRooms == NULL) return 0;
    for (i = 0; i < runtime->activePackage.roomCount; i++) count += runtime->activeRooms[i] != 0;
    return count;
}

#endif /* IOS_O2R_SCENE_RUNTIME */
