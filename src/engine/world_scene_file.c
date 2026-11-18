#include <PR/mbi.h>
#include "world_scene_file.h"

#ifdef IOS_O2R_SCENE_RUNTIME

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/fs/fs.h"
#include "surface_partition.h"

struct SceneReader {
    const u8 *next;
    size_t remaining;
    bool failed;
};

struct FileRoom {
    Vtx *renderVertices;
    Gfx *commands;
    struct WorldRegionVertex *collisionVertices;
    struct WorldRegionTriangle *collisionTriangles;
    u32 vertexCount, triangleCount;
    u32 regionId;
    u32 neighbors[WORLD_SCENE_MAX_ROOM_NEIGHBORS];
    char texture[96];
};

struct FileScene {
    struct WorldSceneRoomDesc *descs;
    struct WorldSceneEntranceDesc *entrances;
    struct WorldSceneExitDesc *exits;
    struct FileRoom *rooms;
    u32 roomCount;
};

static const u8 *read_bytes(struct SceneReader *reader, size_t size) {
    if (reader->failed || reader->remaining < size) {
        reader->failed = true;
        return NULL;
    }
    const u8 *bytes = reader->next;
    reader->next += size;
    reader->remaining -= size;
    return bytes;
}

static u32 read_u32(struct SceneReader *reader) {
    const u8 *p = read_bytes(reader, 4);
    return p == NULL ? 0 : (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 read_u16(struct SceneReader *reader) {
    const u8 *p = read_bytes(reader, 2);
    return p == NULL ? 0 : (u16)(p[0] | (p[1] << 8));
}

static s16 read_s16(struct SceneReader *reader) {
    u16 value = read_u16(reader);
    return value <= 32767 ? (s16)value : (s16)((s32)value - 65536);
}

static struct WorldPoint read_point(struct SceneReader *reader) {
    struct WorldPoint point;
    point.x = read_s16(reader);
    point.y = read_s16(reader);
    point.z = read_s16(reader);
    return point;
}

static bool room_contains(const struct WorldSceneRoomDesc *room, struct WorldPoint p) {
    return p.x >= room->min.x && p.x <= room->max.x
        && p.y >= room->min.y && p.y <= room->max.y
        && p.z >= room->min.z && p.z <= room->max.z;
}

static void release_scene(void *context, struct WorldScenePackage *package) {
    struct FileScene *scene = package->providerData;
    (void)context;
    if (!scene) return;

    if (scene->rooms) {
        for (u32 i = 0; i < scene->roomCount; ++i) {
            struct FileRoom *room = &scene->rooms[i];
#ifdef EXTERNAL_DATA
            if (room->texture[0]) gfx_forget_scene_texture(room->texture);
#endif
            free(room->renderVertices);
            free(room->commands);
            free(room->collisionVertices);
            free(room->collisionTriangles);
        }
    }
    free(scene->descs);
    free(scene->entrances);
    free(scene->exits);
    free(scene->rooms);
    free(scene);
}

static bool read_texture(struct SceneReader *reader, struct FileRoom *room,
                         u16 width, u16 height) {
    u16 length = read_u16(reader);
    if (length == 0) return width == 0 && height == 0 && !reader->failed;
#ifndef EXTERNAL_DATA
    (void)room;
    return false;
#else
    static const char prefix[] = "scenes/";
    static const char suffix[] = ".rgba16";
    if (length >= sizeof(room->texture) || length < sizeof(prefix) + sizeof(suffix) - 1
        || width < 4 || (width & 3) != 0 || !height || (u32)width * height > 2048) return false;
    const u8 *name = read_bytes(reader, length);
    if (!name || memcmp(name, prefix, sizeof(prefix) - 1)
        || memcmp(name + length - (sizeof(suffix) - 1), suffix, sizeof(suffix) - 1)) return false;
    for (size_t i = sizeof(prefix) - 1; i < length - (sizeof(suffix) - 1); ++i) {
        u8 c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    memcpy(room->texture, name, length);
    char path[sizeof(room->texture) + sizeof("gfx/.png")];
    snprintf(path, sizeof(path), "gfx/%s.png", room->texture);
    return fs_sys_file_exists(fs_get_write_path(path));
#endif
}

static void build_display_list(struct FileRoom *room, u32 triangleCount, u16 width, u16 height) {
    Gfx *cmd = room->commands;
    gDPNoOpTag(cmd++, WORLD_SCENE_GFX_TAG);
    gDPPipeSync(cmd++);
    gSPClearGeometryMode(cmd++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    if (room->texture[0]) {
        /* The PC interpreter resolves SETTIMG to a virtual PNG path in EXTERNAL_DATA builds. */
        gDPLoadTextureBlock(cmd++, room->texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, height,
                            0, G_TX_WRAP, G_TX_WRAP, 0, 0, 0, 0);
        gSPTexture(cmd++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombineMode(cmd++, G_CC_MODULATERGB, G_CC_MODULATERGB);
    } else {
        gDPSetCombineMode(cmd++, G_CC_SHADE, G_CC_SHADE);
    }
    for (u32 i = 0; i < triangleCount;) {
        u32 batch = triangleCount - i;
        if (batch > 10) batch = 10; /* 30 of the interpreter's 32 vertex slots. */
        gSPVertex(cmd++, room->renderVertices + 3 * i, batch * 3, 0);
        for (u32 t = 0; t < batch; ++t) {
            gSP1Triangle(cmd++, 3 * t, 3 * t + 1, 3 * t + 2, 0);
        }
        i += batch;
    }
    gSPEndDisplayList(cmd);
}

static bool read_room(struct SceneReader *reader, struct WorldSceneRoomDesc *desc,
                      struct FileRoom *room) {
    desc->id = read_u32(reader);
    desc->min = read_point(reader);
    desc->max = read_point(reader);
    desc->neighborCount = read_u16(reader);
    u32 renderTriangles = read_u16(reader);
    room->vertexCount = read_u16(reader);
    room->triangleCount = read_u16(reader);
    u16 width = read_u16(reader);
    u16 height = read_u16(reader);
    if (reader->failed || desc->neighborCount > WORLD_SCENE_MAX_ROOM_NEIGHBORS
        || !renderTriangles || renderTriangles > WORLD_SCENE_FILE_MAX_TRIANGLES
        || !room->vertexCount || room->vertexCount > WORLD_REGION_MAX_VERTICES
        || !room->triangleCount || room->triangleCount > WORLD_REGION_MAX_TRIANGLES
        || !read_texture(reader, room, width, height)) return false;

    size_t payloadSize = 4 * desc->neighborCount + 42 * renderTriangles
                         + 6 * room->vertexCount + 10 * room->triangleCount;
    if (reader->remaining < payloadSize) return false;

    desc->neighbors = room->neighbors;
    desc->userData = room;
    for (u32 i = 0; i < desc->neighborCount; ++i) room->neighbors[i] = read_u32(reader);

    room->renderVertices = calloc(3 * renderTriangles, sizeof(*room->renderVertices));
    room->commands = calloc(20 + renderTriangles + (renderTriangles + 9) / 10, sizeof(*room->commands));
    room->collisionVertices = calloc(room->vertexCount, sizeof(*room->collisionVertices));
    room->collisionTriangles = calloc(room->triangleCount, sizeof(*room->collisionTriangles));
    if (!room->renderVertices || !room->commands || !room->collisionVertices || !room->collisionTriangles)
        return false;

    for (u32 i = 0; i < 3 * renderTriangles; ++i) {
        Vtx_t *vertex = &room->renderVertices[i].v;
        struct WorldPoint p = read_point(reader);
        if (!room_contains(desc, p)) return false;
        vertex->ob[0] = p.x;
        vertex->ob[1] = p.y;
        vertex->ob[2] = p.z;
        vertex->tc[0] = read_s16(reader);
        vertex->tc[1] = read_s16(reader);
        memcpy(vertex->cn, read_bytes(reader, 4), 4);
    }
    for (u32 i = 0; i < room->vertexCount; ++i) {
        struct WorldPoint p = read_point(reader);
        if (!room_contains(desc, p) || !surface_partition_coord_in_bounds(p.x)
            || !surface_partition_coord_in_bounds(p.z)) return false;
        room->collisionVertices[i] = (struct WorldRegionVertex){p.x, p.y, p.z};
    }
    for (u32 i = 0; i < room->triangleCount; ++i) {
        struct WorldRegionTriangle *tri = &room->collisionTriangles[i];
        tri->a = read_u16(reader);
        tri->b = read_u16(reader);
        tri->c = read_u16(reader);
        tri->type = read_s16(reader);
        tri->force = read_s16(reader);
        if (tri->a >= room->vertexCount || tri->b >= room->vertexCount || tri->c >= room->vertexCount)
            return false;
    }
    build_display_list(room, renderTriangles, width, height);
    return !reader->failed;
}

static s32 load_scene(void *context, u32 id, struct WorldScenePackage *package) {
    char path[48];
    uint64_t size = 0;
    (void)context;
    snprintf(path, sizeof(path), "scenes/%u.wsc", id);
    u8 *bytes = fs_load_write_file(path, WORLD_SCENE_FILE_MAX_BYTES, &size);
    if (!bytes) {
        if (fs_sys_file_exists(fs_get_write_path(path))) {
            fprintf(stderr, "Scene package %u is unreadable or exceeds the size limit\n", id);
        }
        return 0;
    }

    struct SceneReader reader = {bytes, (size_t)size, false};
    const u8 *magic = read_bytes(&reader, 4);
    u32 fileId = read_u32(&reader);
    u16 rooms = read_u16(&reader);
    u16 entrances = read_u16(&reader);
    u16 exits = read_u16(&reader);
    struct FileScene *scene = NULL;
    if (!magic || memcmp(magic, "WSC1", 4) || fileId != id || reader.failed
        || rooms == 0 || rooms > WORLD_REGION_MAX || entrances == 0
        || entrances > WORLD_SCENE_MAX_ENTRANCES || exits > WORLD_SCENE_MAX_EXITS) goto fail;

    scene = calloc(1, sizeof(*scene));
    if (!scene) goto fail;
    package->providerData = scene;
    scene->roomCount = rooms;
    scene->rooms = calloc(rooms, sizeof(*scene->rooms));
    scene->descs = calloc(rooms, sizeof(*scene->descs));
    scene->entrances = calloc(entrances, sizeof(*scene->entrances));
    scene->exits = exits ? calloc(exits, sizeof(*scene->exits)) : NULL;
    if (!scene->rooms || !scene->descs || !scene->entrances || (exits && !scene->exits)) goto fail;

    for (u32 i = 0; i < rooms; ++i) {
        if (!read_room(&reader, &scene->descs[i], &scene->rooms[i])) goto fail;
    }
    for (u32 i = 0; i < entrances; ++i) {
        struct WorldSceneEntranceDesc *entrance = &scene->entrances[i];
        entrance->id = read_u32(&reader);
        entrance->roomId = read_u32(&reader);
        entrance->position = read_point(&reader);
        entrance->yaw = read_s16(&reader);
    }
    for (u32 i = 0; i < exits; ++i) {
        struct WorldSceneExitDesc *exit = &scene->exits[i];
        exit->id = read_u32(&reader);
        exit->destinationSceneId = read_u32(&reader);
        exit->destinationEntranceId = read_u32(&reader);
    }
    if (reader.failed || reader.remaining != 0) goto fail;
    *package = (struct WorldScenePackage){id, scene->descs, rooms, scene->entrances,
                                        entrances, scene->exits, exits, scene};
    free(bytes);
    return 1;

fail:
    fprintf(stderr, "Scene package %u has invalid data near byte %zu\n",
            id, (size_t)(reader.next - bytes));
    release_scene(NULL, package);
    memset(package, 0, sizeof(*package));
    free(bytes);
    return 0;
}

static s32 activate_room(void *context, const struct WorldScenePackage *package,
                         const struct WorldSceneRoomDesc *desc) {
    struct FileRoom *room = (struct FileRoom *)desc->userData;
    struct WorldRegionInfo info;
    (void)context;
    /* Reserve a provider-only ID range. Scene IDs never become collision IDs. */
    u32 id = 0x80000000u;
    while (world_region_info(id, &info)) ++id;

    struct WorldRegionDesc region = {
        .id = id, .vertices = room->collisionVertices, .vertexCount = room->vertexCount,
        .triangles = room->collisionTriangles, .triangleCount = room->triangleCount,
        .renderList = room->commands, .owner = room, .externallyManaged = 1,
    };
    enum WorldRegionResult result = world_region_register(&region);
    if (result != WORLD_REGION_OK) {
        fprintf(stderr, "Scene package %u room %u collision registration failed (%d)\n",
                package->id, desc->id, result);
        return 0;
    }
    if (world_region_activate(id) != WORLD_REGION_OK) {
        world_region_unregister(id);
        return 0;
    }
    room->regionId = id;
    return 1;
}

static void deactivate_room(void *context, const struct WorldScenePackage *package,
                            const struct WorldSceneRoomDesc *desc) {
    struct FileRoom *room = (struct FileRoom *)desc->userData;
    (void)context;
    (void)package;
    world_region_unregister(room->regionId);
    room->regionId = 0;
}

const struct WorldSceneProvider gWorldSceneFileProvider = {
    load_scene, release_scene, activate_room, deactivate_room,
};
#endif
