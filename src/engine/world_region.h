#ifndef WORLD_REGION_H
#define WORLD_REGION_H

#include <PR/ultratypes.h>

/* World coordinates are metadata, not a replacement for SM64's local physics
 * coordinates. The active origin is explicit at every legacy conversion. */
struct WorldPoint {
    s64 x, y, z;
};

s32 world_coord_to_s16(s64 world, s64 activeOrigin, s16 *local);
s32 world_coord_add(s64 origin, s32 local, s64 *world);

#if defined(IOS_CONNECTED_WORLD_TEST)

#define WORLD_REGION_MAX 16
#define WORLD_REGION_MAX_VERTICES 16384
#define WORLD_REGION_MAX_TRIANGLES 8192
#define WORLD_REGION_MAX_NODES 32768
#define WORLD_REGION_MAX_NEIGHBORS 4

struct WorldRegionVertex {
    s32 x, y, z;
};

struct WorldRegionTriangle {
    u32 a, b, c;
    s16 type;
    s16 force;
};

/* The render list must remain valid until world_region_reset and must already
 * use active-area local coordinates matching the translated collision mesh.
 * The descriptor, collision vertices, and triangles are copied into owned
 * metadata and Surface storage during registration. */
struct WorldRegionDesc {
    u32 id;
    struct WorldPoint origin;
    const struct WorldRegionVertex *vertices;
    u32 vertexCount;
    const struct WorldRegionTriangle *triangles;
    u32 triangleCount;
    const void *renderList;
    u32 neighbors[WORLD_REGION_MAX_NEIGHBORS];
    u8 neighborCount;
    void *owner;
};

enum WorldRegionResult {
    WORLD_REGION_OK = 0,
    WORLD_REGION_BAD_ARGUMENT,
    WORLD_REGION_DUPLICATE,
    WORLD_REGION_FULL,
    WORLD_REGION_OUT_OF_RANGE,
    WORLD_REGION_BAD_GEOMETRY,
    WORLD_REGION_NO_MEMORY,
    WORLD_REGION_NOT_FOUND
};

struct WorldRegionInfo {
    u32 id;
    struct WorldPoint min;
    struct WorldPoint max;
    u32 triangleCount;
    u32 nodeCount;
    s32 active;
    const void *renderList;
    u32 neighbors[WORLD_REGION_MAX_NEIGHBORS];
    u8 neighborCount;
    void *owner;
};

/* Reset at area teardown, before the static partition is cleared. The origin
 * is the one used by legacy SM64 collision, camera, and movement this area. */
void world_region_reset(struct WorldPoint activeOrigin);
enum WorldRegionResult world_region_register(const struct WorldRegionDesc *desc);
enum WorldRegionResult world_region_activate(u32 id);
enum WorldRegionResult world_region_deactivate(u32 id);
/* Activates nearby regions first, then retires distant ones. maxStep is the
 * maximum player displacement per simulation tick, including BLJ/vehicles. */
enum WorldRegionResult world_region_update(struct WorldPoint player, u32 maxStep,
                                           u32 prefetchDistance);
/* Adapter for the current area-local Mario coordinates. This does not rebase
 * the live SM64 simulation; out-of-range inputs leave residency unchanged. */
void world_region_update_legacy(f32 x, f32 y, f32 z, f32 vx, f32 vz,
                                f32 forwardSpeed, u32 prefetchDistance);
s32 world_region_info(u32 id, struct WorldRegionInfo *info);
s32 world_region_info_at(u32 index, struct WorldRegionInfo *info);
/* Conservative AABB distance cull. Projection/frustum clipping remains with
 * SM64's normal geo renderer; a false result never unloads collision. */
s32 world_region_render_near_legacy(const struct WorldRegionInfo *info,
                                    f32 cameraX, f32 cameraY, f32 cameraZ, u32 farDistance);
u32 world_region_count(void);

#endif
#endif
