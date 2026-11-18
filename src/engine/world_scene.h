#ifndef WORLD_SCENE_H
#define WORLD_SCENE_H

#if defined(IOS_O2R_SCENE_RUNTIME)

#include <PR/ultratypes.h>
#include "world_region.h"

/* A scene package is supplied by a platform/content provider. Scene IDs and
 * room IDs are stable package-local identifiers; the manager does not know
 * about any particular game's resource paths or asset formats. */
#define WORLD_SCENE_MAX_ROOMS 4096
#define WORLD_SCENE_MAX_ENTRANCES 4096
#define WORLD_SCENE_MAX_EXITS 4096
#define WORLD_SCENE_MAX_ROOM_NEIGHBORS 16

struct WorldSceneRoomDesc {
    u32 id;
    struct WorldPoint min;
    struct WorldPoint max;
    const u32 *neighbors;
    u32 neighborCount;
    const void *userData;
};

struct WorldSceneEntranceDesc {
    u32 id;
    u32 roomId;
    struct WorldPoint position;
    s16 yaw;
};

struct WorldSceneExitDesc {
    u32 id;
    u32 destinationSceneId;
    u32 destinationEntranceId;
};

struct WorldScenePackage {
    u32 id;
    const struct WorldSceneRoomDesc *rooms;
    u32 roomCount;
    const struct WorldSceneEntranceDesc *entrances;
    u32 entranceCount;
    const struct WorldSceneExitDesc *exits;
    u32 exitCount;
    void *providerData;
};

enum WorldSceneResult {
    WORLD_SCENE_OK = 0,
    WORLD_SCENE_BAD_ARGUMENT,
    WORLD_SCENE_NOT_OPEN,
    WORLD_SCENE_NOT_FOUND,
    WORLD_SCENE_BAD_PACKAGE,
    WORLD_SCENE_NO_MEMORY,
    WORLD_SCENE_PROVIDER_FAILED,
    WORLD_SCENE_PLAYER_OUTSIDE
};

struct WorldSceneProvider {
    /* loadScene fills a complete, independently owned package. Every success
     * has one releaseScene obligation; a provider may cache via references.
     * A failed load cleans up its own partial allocations and cannot mutate
     * the currently active package. */
    s32 (*loadScene)(void *context, u32 sceneId, struct WorldScenePackage *out);
    void (*releaseScene)(void *context, struct WorldScenePackage *package);
    /* Room activation owns renderer/collision registration. On failure it
     * must leave that room inactive (rolling back partial work itself). It
     * must be safe to activate a neighbor before retiring the prior room. */
    s32 (*activateRoom)(void *context, const struct WorldScenePackage *package,
                        const struct WorldSceneRoomDesc *room);
    void (*deactivateRoom)(void *context, const struct WorldScenePackage *package,
                           const struct WorldSceneRoomDesc *room);
};

struct WorldSceneRuntime {
    struct WorldSceneProvider provider;
    void *providerContext;
    struct WorldScenePackage activePackage;
    u8 *activeRooms;
    u32 currentRoomId;
    s32 isOpen;
};

struct WorldSceneDestination {
    u32 sceneId;
    u32 roomId;
    u32 entranceId;
    struct WorldPoint position;
    s16 yaw;
};

/* The caller owns Mario/camera state. Scene changes return a destination
 * spawn and never reset gameplay state themselves. All calls are main-thread
 * only; GPU/resource destruction must be deferred by the provider if needed. */
void world_scene_runtime_init(struct WorldSceneRuntime *runtime,
                              const struct WorldSceneProvider *provider,
                              void *providerContext);
enum WorldSceneResult world_scene_open(struct WorldSceneRuntime *runtime,
                                       u32 sceneId, u32 entranceId,
                                       struct WorldSceneDestination *destination);
enum WorldSceneResult world_scene_traverse_exit(struct WorldSceneRuntime *runtime,
                                                u32 exitId,
                                                struct WorldSceneDestination *destination);
enum WorldSceneResult world_scene_transition(struct WorldSceneRuntime *runtime,
                                             u32 sceneId, u32 entranceId,
                                             struct WorldSceneDestination *destination);
/* Activate rooms near the player before retiring distant rooms. maxStep is a
 * conservative bound on displacement during one simulation tick. The active
 * room and its declared neighbors are pinned to close boundary seams. */
enum WorldSceneResult world_scene_update_residency(struct WorldSceneRuntime *runtime,
                                                   struct WorldPoint player,
                                                   u32 maxStep,
                                                   u32 prefetchDistance);
void world_scene_close(struct WorldSceneRuntime *runtime);
u32 world_scene_active_id(const struct WorldSceneRuntime *runtime);
u32 world_scene_current_room(const struct WorldSceneRuntime *runtime);
u32 world_scene_active_room_count(const struct WorldSceneRuntime *runtime);

#endif /* IOS_O2R_SCENE_RUNTIME */
#endif
