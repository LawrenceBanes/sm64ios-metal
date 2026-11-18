#include "world_region.h"

#include <limits.h>

s32 world_coord_to_s16(s64 world, s64 activeOrigin, s16 *local) {
    u64 distance;
    if (local == NULL) return 0;
    if (world >= activeOrigin) {
        distance = (u64)world - (u64)activeOrigin;
        if (distance > 32767) return 0;
        *local = (s16)distance;
    } else {
        distance = (u64)activeOrigin - (u64)world;
        if (distance > 32768) return 0;
        *local = (s16)-(s32)distance;
    }
    return 1;
}

s32 world_coord_add(s64 origin, s32 local, s64 *world) {
    if (world == NULL) return 0;
    if ((local > 0 && origin > LLONG_MAX - (s64)local)
        || (local < 0 && origin < LLONG_MIN - (s64)local)) return 0;
    *world = origin + local;
    return 1;
}

#if defined(IOS_CONNECTED_WORLD_TEST) || defined(IOS_O2R_SCENE_RUNTIME)

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "surface_load.h"
#include "surface_terrains.h"

struct WorldRegionNode {
    struct SurfaceNode node;
    s16 cellX, cellZ;
    u8 category;
};

struct WorldRegionSlot {
    struct WorldRegionInfo info;
    struct Surface *surfaces;
    struct WorldRegionNode *nodes;
    s32 used;
};

static struct WorldPoint sActiveOrigin;
static struct WorldRegionSlot sRegions[WORLD_REGION_MAX];
static u32 sRegionCount;

static s16 clamp_s16(s32 value) {
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return (s16)value;
}

/* Match the legacy loader's 50-unit cell overlap exactly. */
static s16 lower_cell(s16 coord) {
    s32 shifted = (s32)coord + LEVEL_BOUNDARY_MAX;
    s32 cell;
    if (shifted < 0) shifted = 0;
    cell = shifted / CELL_SIZE;
    if (shifted % CELL_SIZE < 50) cell--;
    if (cell < 0) cell = 0;
    if (cell >= SURFACE_PARTITION_DIM) cell = SURFACE_PARTITION_DIM - 1;
    return (s16)cell;
}

static s16 upper_cell(s16 coord) {
    s32 shifted = (s32)coord + LEVEL_BOUNDARY_MAX;
    s32 cell;
    if (shifted < 0) shifted = 0;
    cell = shifted / CELL_SIZE;
    if (shifted % CELL_SIZE > CELL_SIZE - 50) cell++;
    if (cell >= SURFACE_PARTITION_DIM) cell = SURFACE_PARTITION_DIM - 1;
    return (s16)cell;
}

static u8 category_for(const struct Surface *surface) {
    if (surface->normal.y > 0.01f) return SPATIAL_PARTITION_FLOORS;
    if (surface->normal.y < -0.01f) return SPATIAL_PARTITION_CEILS;
    return SPATIAL_PARTITION_WALLS;
}

static s32 build_surface(struct Surface *out, const Vec3s a, const Vec3s b,
                         const Vec3s c, const struct WorldRegionTriangle *triangle) {
    f64 nx = (f64)(b[1] - a[1]) * (c[2] - b[2]) - (f64)(b[2] - a[2]) * (c[1] - b[1]);
    f64 ny = (f64)(b[2] - a[2]) * (c[0] - b[0]) - (f64)(b[0] - a[0]) * (c[2] - b[2]);
    f64 nz = (f64)(b[0] - a[0]) * (c[1] - b[1]) - (f64)(b[1] - a[1]) * (c[0] - b[0]);
    f64 magnitude = sqrt(nx * nx + ny * ny + nz * nz);
    s32 minY, maxY;
    if (magnitude < 0.0001) return 0;
    memset(out, 0, sizeof(*out));
    memcpy(out->vertex1, a, sizeof(Vec3s));
    memcpy(out->vertex2, b, sizeof(Vec3s));
    memcpy(out->vertex3, c, sizeof(Vec3s));
    out->normal.x = (f32)(nx / magnitude);
    out->normal.y = (f32)(ny / magnitude);
    out->normal.z = (f32)(nz / magnitude);
    out->originOffset = -(out->normal.x * a[0] + out->normal.y * a[1]
                          + out->normal.z * a[2]);
    minY = a[1] < b[1] ? a[1] : b[1];
    if (c[1] < minY) minY = c[1];
    maxY = a[1] > b[1] ? a[1] : b[1];
    if (c[1] > maxY) maxY = c[1];
    out->lowerY = clamp_s16(minY - 5);
    out->upperY = clamp_s16(maxY + 5);
    out->type = triangle->type;
    out->force = triangle->force;
    if (out->normal.y <= 0.01f && out->normal.y >= -0.01f
        && (out->normal.x < -0.707f || out->normal.x > 0.707f)) {
        out->flags |= SURFACE_FLAG_X_PROJECTION;
    }
    if (triangle->type == SURFACE_NO_CAM_COLLISION
        || triangle->type == SURFACE_NO_CAM_COLLISION_77
        || triangle->type == SURFACE_NO_CAM_COL_VERY_SLIPPERY
        || triangle->type == SURFACE_SWITCH) {
        out->flags |= SURFACE_FLAG_NO_CAM_COLLISION;
    }
    return 1;
}

static struct WorldRegionSlot *find_slot(u32 id) {
    u32 i;
    for (i = 0; i < sRegionCount; i++) {
        if (sRegions[i].used && sRegions[i].info.id == id) return &sRegions[i];
    }
    return NULL;
}

static void attach_node(struct WorldRegionNode *entry) {
    struct SurfaceNode *list = &gStaticSurfacePartition[entry->cellZ][entry->cellX][entry->category];
    s32 sortDir = entry->category == SPATIAL_PARTITION_FLOORS ? 1
                  : entry->category == SPATIAL_PARTITION_CEILS ? -1 : 0;
    s32 priority = entry->node.surface->vertex1[1] * sortDir;
    while (list->next != NULL) {
        if (priority > list->next->surface->vertex1[1] * sortDir) break;
        list = list->next;
    }
    entry->node.next = list->next;
    list->next = &entry->node;
}

static void detach_node(struct WorldRegionNode *entry) {
    struct SurfaceNode *list = &gStaticSurfacePartition[entry->cellZ][entry->cellX][entry->category];
    while (list->next != NULL && list->next != &entry->node) list = list->next;
    if (list->next == &entry->node) list->next = entry->node.next;
    entry->node.next = NULL;
}

void world_region_reset(struct WorldPoint activeOrigin) {
    u32 i;
    for (i = 0; i < sRegionCount; i++) {
        u32 node;
        if (sRegions[i].info.active) {
            for (node = 0; node < sRegions[i].info.nodeCount; node++) {
                detach_node(&sRegions[i].nodes[node]);
            }
        }
        free(sRegions[i].nodes);
        free(sRegions[i].surfaces);
    }
    memset(sRegions, 0, sizeof(sRegions));
    sRegionCount = 0;
    sActiveOrigin = activeOrigin;
}

enum WorldRegionResult world_region_register(const struct WorldRegionDesc *desc) {
    Vec3s *vertices = NULL;
    struct Surface *surfaces = NULL;
    struct WorldRegionNode *nodes = NULL;
    struct WorldRegionInfo info;
    u32 i, nodeCount = 0, cursor = 0;
    if (desc == NULL || desc->id == 0 || desc->vertexCount == 0
        || desc->triangleCount == 0 || desc->vertices == NULL || desc->triangles == NULL
        || desc->vertexCount > WORLD_REGION_MAX_VERTICES
        || desc->triangleCount > WORLD_REGION_MAX_TRIANGLES
        || desc->neighborCount > WORLD_REGION_MAX_NEIGHBORS) return WORLD_REGION_BAD_ARGUMENT;
    if (find_slot(desc->id) != NULL) return WORLD_REGION_DUPLICATE;
    if (sRegionCount == WORLD_REGION_MAX) return WORLD_REGION_FULL;
    for (i = 0; i < desc->neighborCount; i++) {
        u32 j;
        if (desc->neighbors[i] == 0 || desc->neighbors[i] == desc->id)
            return WORLD_REGION_BAD_ARGUMENT;
        for (j = 0; j < i; j++) {
            if (desc->neighbors[j] == desc->neighbors[i]) return WORLD_REGION_BAD_ARGUMENT;
        }
    }

    vertices = calloc(desc->vertexCount, sizeof(*vertices));
    surfaces = calloc(desc->triangleCount, sizeof(*surfaces));
    if (vertices == NULL || surfaces == NULL) goto no_memory;
    memset(&info, 0, sizeof(info));
    info.id = desc->id;
    info.renderList = desc->renderList;
    info.translucentList = desc->translucentList;
    info.owner = desc->owner;
    info.externallyManaged = desc->externallyManaged;
    info.neighborCount = desc->neighborCount;
    memcpy(info.neighbors, desc->neighbors, desc->neighborCount * sizeof(desc->neighbors[0]));
    info.triangleCount = desc->triangleCount;

    for (i = 0; i < desc->vertexCount; i++) {
        s64 world[3];
        const s32 local[3] = {desc->vertices[i].x, desc->vertices[i].y,
                              desc->vertices[i].z};
        const s64 origin[3] = {desc->origin.x, desc->origin.y, desc->origin.z};
        const s64 active[3] = {sActiveOrigin.x, sActiveOrigin.y, sActiveOrigin.z};
        u32 axis;
        for (axis = 0; axis < 3; axis++) {
            if (!world_coord_add(origin[axis], local[axis], &world[axis])
                || !world_coord_to_s16(world[axis], active[axis], &vertices[i][axis])) {
                free(vertices); free(surfaces); return WORLD_REGION_OUT_OF_RANGE;
            }
        }
        if (!surface_partition_coord_in_bounds(vertices[i][0])
            || !surface_partition_coord_in_bounds(vertices[i][2])) {
            free(vertices); free(surfaces); return WORLD_REGION_OUT_OF_RANGE;
        }
        if (i == 0) {
            info.min.x = info.max.x = world[0];
            info.min.y = info.max.y = world[1];
            info.min.z = info.max.z = world[2];
        } else {
            if (world[0] < info.min.x) info.min.x = world[0];
            if (world[0] > info.max.x) info.max.x = world[0];
            if (world[1] < info.min.y) info.min.y = world[1];
            if (world[1] > info.max.y) info.max.y = world[1];
            if (world[2] < info.min.z) info.min.z = world[2];
            if (world[2] > info.max.z) info.max.z = world[2];
        }
    }
    for (i = 0; i < desc->triangleCount; i++) {
        const struct WorldRegionTriangle *tri = &desc->triangles[i];
        struct Surface *surface = &surfaces[i];
        s16 minX, minZ, maxX, maxZ;
        s16 minCellX, minCellZ, maxCellX, maxCellZ;
        if (tri->a >= desc->vertexCount || tri->b >= desc->vertexCount
            || tri->c >= desc->vertexCount || tri->a == tri->b || tri->b == tri->c
            || tri->a == tri->c || !(TERRAIN_LOAD_IS_SURFACE_TYPE_LOW(tri->type)
                                      || TERRAIN_LOAD_IS_SURFACE_TYPE_HIGH(tri->type))
            || !build_surface(surface, vertices[tri->a], vertices[tri->b],
                              vertices[tri->c], tri)) {
            free(vertices); free(surfaces); return WORLD_REGION_BAD_GEOMETRY;
        }
        minX = surface->vertex1[0] < surface->vertex2[0] ? surface->vertex1[0] : surface->vertex2[0];
        if (surface->vertex3[0] < minX) minX = surface->vertex3[0];
        maxX = surface->vertex1[0] > surface->vertex2[0] ? surface->vertex1[0] : surface->vertex2[0];
        if (surface->vertex3[0] > maxX) maxX = surface->vertex3[0];
        minZ = surface->vertex1[2] < surface->vertex2[2] ? surface->vertex1[2] : surface->vertex2[2];
        if (surface->vertex3[2] < minZ) minZ = surface->vertex3[2];
        maxZ = surface->vertex1[2] > surface->vertex2[2] ? surface->vertex1[2] : surface->vertex2[2];
        if (surface->vertex3[2] > maxZ) maxZ = surface->vertex3[2];
        minCellX = lower_cell(minX); maxCellX = upper_cell(maxX);
        minCellZ = lower_cell(minZ); maxCellZ = upper_cell(maxZ);
        if ((u32)(maxCellX - minCellX + 1) * (maxCellZ - minCellZ + 1)
            > WORLD_REGION_MAX_NODES - nodeCount) {
            free(vertices); free(surfaces); return WORLD_REGION_BAD_GEOMETRY;
        }
        nodeCount += (maxCellX - minCellX + 1) * (maxCellZ - minCellZ + 1);
    }
    nodes = calloc(nodeCount, sizeof(*nodes));
    if (nodes == NULL) goto no_memory;
    for (i = 0; i < desc->triangleCount; i++) {
        struct Surface *surface = &surfaces[i];
        s16 minX = surface->vertex1[0], maxX = minX;
        s16 minZ = surface->vertex1[2], maxZ = minZ;
        s16 cx, cz;
        u32 vertex;
        for (vertex = 1; vertex < 3; vertex++) {
            const s16 *v = vertex == 1 ? surface->vertex2 : surface->vertex3;
            if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
            if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
        }
        for (cz = lower_cell(minZ); cz <= upper_cell(maxZ); cz++) {
            for (cx = lower_cell(minX); cx <= upper_cell(maxX); cx++) {
                nodes[cursor].cellX = cx;
                nodes[cursor].cellZ = cz;
                nodes[cursor].category = category_for(surface);
                nodes[cursor].node.surface = surface;
                cursor++;
            }
        }
    }
    free(vertices);
    info.nodeCount = nodeCount;
    sRegions[sRegionCount].info = info;
    sRegions[sRegionCount].surfaces = surfaces;
    sRegions[sRegionCount].nodes = nodes;
    sRegions[sRegionCount].used = 1;
    sRegionCount++;
    return WORLD_REGION_OK;

no_memory:
    free(vertices); free(surfaces); free(nodes);
    return WORLD_REGION_NO_MEMORY;
}

enum WorldRegionResult world_region_activate(u32 id) {
    struct WorldRegionSlot *slot = find_slot(id);
    u32 i;
    if (slot == NULL) return WORLD_REGION_NOT_FOUND;
    if (slot->info.active) return WORLD_REGION_OK;
    for (i = 0; i < slot->info.nodeCount; i++) attach_node(&slot->nodes[i]);
    slot->info.active = 1;
    return WORLD_REGION_OK;
}

enum WorldRegionResult world_region_unregister(u32 id) {
    struct WorldRegionSlot *slot = find_slot(id);
    u32 index, node;
    if (slot == NULL) return WORLD_REGION_NOT_FOUND;
    index = (u32)(slot - sRegions);
    if (slot->info.active) {
        for (node = 0; node < slot->info.nodeCount; node++) {
            detach_node(&slot->nodes[node]);
        }
    }
    free(slot->nodes);
    free(slot->surfaces);
    if (index + 1 < sRegionCount) {
        memmove(&sRegions[index], &sRegions[index + 1],
                (sRegionCount - index - 1) * sizeof(sRegions[0]));
    }
    sRegionCount--;
    memset(&sRegions[sRegionCount], 0, sizeof(sRegions[0]));
    return WORLD_REGION_OK;
}

enum WorldRegionResult world_region_deactivate(u32 id) {
    struct WorldRegionSlot *slot = find_slot(id);
    u32 i;
    if (slot == NULL) return WORLD_REGION_NOT_FOUND;
    if (!slot->info.active) return WORLD_REGION_OK;
    for (i = 0; i < slot->info.nodeCount; i++) detach_node(&slot->nodes[i]);
    slot->info.active = 0;
    return WORLD_REGION_OK;
}

static u64 outside(s64 coordinate, s64 low, s64 high) {
    if (coordinate < low) return (u64)low - (u64)coordinate;
    if (coordinate > high) return (u64)coordinate - (u64)high;
    return 0;
}

enum WorldRegionResult world_region_update(struct WorldPoint player, u32 maxStep,
                                           u32 prefetchDistance) {
    u32 i;
    u8 keepNeighbor[WORLD_REGION_MAX] = {0};
    u64 loadDistance = (u64)prefetchDistance + 2u * (u64)maxStep;
    u64 unloadDistance = loadDistance + prefetchDistance + CELL_SIZE;
    for (i = 0; i < sRegionCount; i++) {
        struct WorldRegionInfo *info = &sRegions[i].info;
        if (info->externallyManaged) continue;
        u64 dx = outside(player.x, info->min.x, info->max.x);
        u64 dz = outside(player.z, info->min.z, info->max.z);
        if (dx <= loadDistance && dz <= loadDistance) world_region_activate(info->id);
    }
    /* The region occupied by the player pins its declared neighbors even if
     * a sparse mesh leaves their AABB farther than the prefetch radius. */
    for (i = 0; i < sRegionCount; i++) {
        const struct WorldRegionInfo *info = &sRegions[i].info;
        u32 neighbor;
        if (info->externallyManaged || outside(player.x, info->min.x, info->max.x) != 0
            || outside(player.z, info->min.z, info->max.z) != 0) continue;
        for (neighbor = 0; neighbor < info->neighborCount; neighbor++) {
            struct WorldRegionSlot *slot = find_slot(info->neighbors[neighbor]);
            if (slot != NULL && !slot->info.externallyManaged) {
                u32 index = (u32)(slot - sRegions);
                keepNeighbor[index] = 1;
                world_region_activate(slot->info.id);
            }
        }
    }
    for (i = 0; i < sRegionCount; i++) {
        struct WorldRegionInfo *info = &sRegions[i].info;
        if (info->externallyManaged) continue;
        u64 dx = outside(player.x, info->min.x, info->max.x);
        u64 dz = outside(player.z, info->min.z, info->max.z);
        if (!keepNeighbor[i] && (dx > unloadDistance || dz > unloadDistance))
            world_region_deactivate(info->id);
    }
    return WORLD_REGION_OK;
}

void world_region_update_legacy(f32 x, f32 y, f32 z, f32 vx, f32 vz,
                                f32 forwardSpeed, u32 prefetchDistance) {
    struct WorldPoint player;
    f32 speed;
    if (sRegionCount == 0 || !isfinite(x) || !isfinite(y) || !isfinite(z)
        || !surface_partition_float_in_bounds(x)
        || !surface_partition_float_in_bounds(z) || y < -32768.0f || y >= 32768.0f) return;
    if (!world_coord_add(sActiveOrigin.x, (s32)x, &player.x)
        || !world_coord_add(sActiveOrigin.y, (s32)y, &player.y)
        || !world_coord_add(sActiveOrigin.z, (s32)z, &player.z)) return;
    speed = fabsf(vx);
    if (fabsf(vz) > speed) speed = fabsf(vz);
    if (fabsf(forwardSpeed) > speed) speed = fabsf(forwardSpeed);
    /* A very large BLJ or vehicle impulse activates every region in the
     * current s16 window before movement instead of underestimating speed. */
    if (!(speed >= 0.0f) || speed > 65535.0f) speed = 65535.0f;
    world_region_update(player, (u32)speed + 1024, prefetchDistance);
}

s32 world_region_info(u32 id, struct WorldRegionInfo *info) {
    struct WorldRegionSlot *slot = find_slot(id);
    if (slot == NULL || info == NULL) return 0;
    *info = slot->info;
    return 1;
}

s32 world_region_info_at(u32 index, struct WorldRegionInfo *info) {
    if (index >= sRegionCount || info == NULL) return 0;
    *info = sRegions[index].info;
    return 1;
}

s32 world_region_render_near_legacy(const struct WorldRegionInfo *info,
                                    f32 cameraX, f32 cameraY, f32 cameraZ, u32 farDistance) {
    struct WorldPoint camera;
    u64 dx, dy, dz, remaining;
    if (info == NULL || !info->active || info->renderList == NULL) return 0;
    if (!isfinite(cameraX) || !isfinite(cameraY) || !isfinite(cameraZ)
        || cameraX < -32768.0f || cameraX >= 32768.0f
        || cameraY < -32768.0f || cameraY >= 32768.0f
        || cameraZ < -32768.0f || cameraZ >= 32768.0f) return 1;
    if (!world_coord_add(sActiveOrigin.x, (s32)cameraX, &camera.x)
        || !world_coord_add(sActiveOrigin.y, (s32)cameraY, &camera.y)
        || !world_coord_add(sActiveOrigin.z, (s32)cameraZ, &camera.z)) return 1;
    dx = outside(camera.x, info->min.x, info->max.x);
    dy = outside(camera.y, info->min.y, info->max.y);
    dz = outside(camera.z, info->min.z, info->max.z);
    if (dx > farDistance || dy > farDistance || dz > farDistance) return 0;
    remaining = (u64)farDistance * farDistance;
    if (dx * dx > remaining) return 0;
    remaining -= dx * dx;
    if (dy * dy > remaining) return 0;
    remaining -= dy * dy;
    return dz * dz <= remaining;
}

u32 world_region_count(void) { return sRegionCount; }

#endif
