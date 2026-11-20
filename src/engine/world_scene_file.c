#include <PR/mbi.h>
#include "world_scene_file.h"
#include "scene_diagnostic.h"

#ifdef IOS_O2R_SCENE_RUNTIME

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/fs/fs.h"
#include "pc/gfx/gfx_render_section.h"
#include "surface_partition.h"
#if defined(IOS_BUILD) && defined(IOS_SCENE_DIAGNOSTIC)
#include "pc/gfx/gfx_ios_diagnostics.h"
#define WSC_DIAG_LOG(...) gfx_ios_diag_log(__VA_ARGS__)
#else
#define WSC_DIAG_LOG(...) ((void)0)
#endif

struct SceneReader {
    const u8 *next;
    size_t remaining;
    bool failed;
};

#define WORLD_SCENE_MAX_BATCHES_LEGACY 64
#define WORLD_SCENE_MAX_BATCHES_WSC5 256

struct FileBatch {
    Vtx *vertices;
    u16 triangleCount, width, height;
    u16 pass, state;
    char texture[96];
    struct WorldSceneMaterial *material;
};

struct FileRoom {
    struct FileBatch *batches;
    struct WorldSceneDrawState *states;
    u16 batchCount;
    Gfx *commands[2];
    struct WorldRegionVertex *collisionVertices;
    struct WorldRegionTriangle *collisionTriangles;
    u32 vertexCount, triangleCount;
    u32 regionId;
    u32 neighbors[WORLD_SCENE_MAX_ROOM_NEIGHBORS];
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

static void invalidate_room_textures(const struct FileRoom *room) {
#ifdef EXTERNAL_DATA
    for (u16 j = 0; room->batches && j < room->batchCount; ++j) {
        const struct FileBatch *batch = &room->batches[j];
        if (batch->texture[0]) gfx_forget_scene_texture(batch->texture);
        if (batch->material) {
            for (int tile = 0; tile < 2; ++tile) {
                const char *name = batch->material->texture[tile].name;
                if (name[0]) gfx_forget_scene_texture(name);
            }
        }
    }
#else
    (void)room;
#endif
}

static void release_scene(void *context, struct WorldScenePackage *package) {
    struct FileScene *scene = package->providerData;
    (void)context;
    if (!scene) return;
    fs_scene_library_reset();

    if (scene->rooms) {
        for (u32 i = 0; i < scene->roomCount; ++i) {
            struct FileRoom *room = &scene->rooms[i];
            invalidate_room_textures(room);
            for (u16 j = 0; room->batches && j < room->batchCount; ++j) {
                free(room->batches[j].vertices);
                free(room->batches[j].material);
            }
            free(room->batches);
            free(room->states);
            free(room->commands[0]);
            free(room->commands[1]);
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

static bool read_texture(struct SceneReader *reader, char texture[96], u16 width, u16 height,
                         bool legacy, bool wsc5Format) {
    u16 length = read_u16(reader);
    if (length == 0) return width == 0 && height == 0 && !reader->failed;
#ifndef EXTERNAL_DATA
    (void)texture;
    (void)legacy;
    (void)wsc5Format;
    return false;
#else
    static const char prefix[] = "scenes/";
    static const char suffix[] = ".rgba16";
    if (length >= 96 || length < sizeof(prefix) + sizeof(suffix) - 1
        || width < 4 || (width & 3) != 0 || !height
        || (u32)width * height > (wsc5Format ? 4096u : 2048u)) return false;
    const u8 *name = read_bytes(reader, length);
    if (!name || memcmp(name, prefix, sizeof(prefix) - 1)
        || memcmp(name + length - (sizeof(suffix) - 1), suffix, sizeof(suffix) - 1)) return false;
    for (size_t i = sizeof(prefix) - 1; i < length - (sizeof(suffix) - 1); ++i) {
        u8 c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    memcpy(texture, name, length);
    char path[96 + sizeof("gfx/.png")];
    snprintf(path, sizeof(path), "gfx/%s.png", texture);
    if (legacy) return fs_sys_file_exists(fs_get_write_path(path));
    if (!gfx_validate_scene_texture(path, width, height)) {
        fprintf(stderr, "Scene package texture cannot decode as %ux%u PNG: %s\n",
                (unsigned)width, (unsigned)height, path);
        WSC_DIAG_LOG("Scene texture reference rejected path=%s expected=%ux%u",
                     path, (unsigned)width, (unsigned)height);
        return false;
    }
    return true;
#endif
}

static bool read_material(struct SceneReader *reader, struct FileBatch *batch,
                         bool fogFormat, bool decalFormat) {
    struct WorldSceneMaterial *material = batch->material = calloc(1, sizeof(*material));
    if (!material) return false;
    u16 flags = read_u16(reader);
    const u8 *colors = read_bytes(reader, 8);
    u16 supportedFlags = decalFormat ? 127u : fogFormat ? 63u : 31u;
    bool blend = (flags & WORLD_SCENE_BLEND) != 0;
    bool decal = (flags & WORLD_SCENE_DECAL) != 0;
    bool blendPassValid = decalFormat
        ? (batch->pass ? blend : (!blend || decal)) && (!decal || blend)
        : blend == !!batch->pass;
    if (!colors || (flags & ~supportedFlags) || !blendPassValid
        || ((flags & WORLD_SCENE_BLEND) && (flags & WORLD_SCENE_CUTOUT))) return false;
    material->flags = flags;
    memcpy(material->primitive, colors, 4);
    memcpy(material->environment, colors + 4, 4);
    material->alphaPeriod = read_u16(reader);
    u16 peak = read_u16(reader);
    if (peak > 255 || (material->alphaPeriod && material->alphaPeriod < 2)) return false;
    material->alphaPeak = peak;
    for (int index = 0; index < 2; ++index) {
        struct WorldSceneTexture *texture = &material->texture[index];
        texture->width = read_u16(reader);
        texture->height = read_u16(reader);
        u16 sampler = read_u16(reader);
        if (sampler > 31 || (sampler & 3) == 3 || ((sampler >> 2) & 3) == 3) return false;
        texture->address[0] = sampler & 3;
        texture->address[1] = (sampler >> 2) & 3;
        texture->filter = (sampler >> 4) & 1;
        for (int i = 0; i < 2; ++i) texture->scale[i] = read_s16(reader);
        for (int i = 0; i < 2; ++i) texture->origin[i] = read_s16(reader);
        for (int i = 0; i < 2; ++i) texture->scroll[i] = read_s16(reader);
        if (!read_texture(reader, texture->name, texture->width, texture->height, false, decalFormat)) return false;
    }
    if (material->flags & WORLD_SCENE_FOG) {
        if (!fogFormat) return false;
        material->fogMultiplier = read_s16(reader);
        material->fogOffset = read_s16(reader);
        const u8 *fogColor = read_bytes(reader, sizeof(material->fogColor));
        if (!fogColor) return false;
        memcpy(material->fogColor, fogColor, sizeof(material->fogColor));
    }
    return !material->texture[1].name[0] || material->texture[0].name[0];
}

static Gfx *emit_batch(Gfx *cmd, const struct FileBatch *batch,
                       const struct WorldSceneDrawState *states, bool legacy) {
    gDPPipeSync(cmd++);
    if (batch->material) {
        cmd->words.w0 = WORLD_SCENE_MATERIAL_OPCODE;
        cmd->words.w1 = (uintptr_t)batch->material;
        ++cmd;
        gSPGeometryMode(cmd++, G_CULL_BOTH, (batch->material->flags & WORLD_SCENE_BACK_CULL) ? G_CULL_BACK : 0);
    } else if (!legacy) {
        cmd->words.w0 = WORLD_SCENE_STATE_OPCODE;
        cmd->words.w1 = batch->state ? (uintptr_t)&states[batch->state - 1] : 0;
        ++cmd;
    }
    if (!batch->material && batch->texture[0]) {
        gDPLoadTextureBlock(cmd++, batch->texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, batch->width, batch->height,
                            0, G_TX_WRAP, G_TX_WRAP, 0, 0, 0, 0);
        gSPTexture(cmd++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
        if (batch->pass) {
            gDPSetCombineMode(cmd++, G_CC_MODULATEIA, G_CC_MODULATEIA);
        } else {
            gDPSetCombineMode(cmd++, G_CC_MODULATERGB, G_CC_MODULATERGB);
        }
    } else if (!batch->material) {
        gDPSetCombineMode(cmd++, G_CC_SHADE, G_CC_SHADE);
    }
    for (u32 i = 0; i < batch->triangleCount;) {
        u32 count = batch->triangleCount - i;
        if (count > 10) count = 10; /* Keep below the interpreter's 32 vertex slots. */
        gSPVertex(cmd++, batch->vertices + 3 * i, count * 3, 0);
        for (u32 t = 0; t < count; ++t) {
            gSP1Triangle(cmd++, 3 * t, 3 * t + 1, 3 * t + 2, 0);
        }
        i += count;
    }
    return cmd;
}

static bool build_display_lists(struct FileRoom *room, bool legacy) {
    for (u16 pass = 0; pass < 2; ++pass) {
        size_t capacity = 0;
        for (u16 i = 0; i < room->batchCount; ++i) {
            const struct FileBatch *batch = &room->batches[i];
            if (batch->pass == pass) capacity += 16 + batch->triangleCount + (batch->triangleCount + 9) / 10;
        }
        if (!capacity) continue;
        Gfx *cmd = room->commands[pass] = calloc(8 + capacity, sizeof(*cmd));
        if (!cmd) return false;
        gDPNoOpTag(cmd++, WORLD_SCENE_GFX_TAG);
        u16 section = pass ? GFX_RENDER_SCENE_TRANSLUCENT : GFX_RENDER_SCENE_OPAQUE;
        if (!legacy) gDPNoOpTag(cmd++, GFX_RENDER_SECTION_BEGIN_TAG | section);
        gDPPipeSync(cmd++);
        gSPClearGeometryMode(cmd++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
        if (!legacy) {
            gSPSetGeometryMode(cmd++, G_ZBUFFER);
            if (pass) {
                gDPSetRenderMode(cmd++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
            } else {
                gDPSetRenderMode(cmd++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
            }
        }
        for (u16 i = 0; i < room->batchCount; ++i) {
            if (room->batches[i].pass == pass) cmd = emit_batch(cmd, &room->batches[i], room->states, legacy);
        }
        if (!legacy) gDPNoOpTag(cmd++, GFX_RENDER_SECTION_END_TAG | section);
        gSPEndDisplayList(cmd);
    }
    return true;
}

static bool read_render_vertices(struct SceneReader *reader, const struct WorldSceneRoomDesc *room,
                                 struct FileBatch *batch) {
    u32 count = 3u * batch->triangleCount;
    if (!count || reader->remaining < 14u * count) return false;
    batch->vertices = calloc(count, sizeof(*batch->vertices));
    if (!batch->vertices) return false;
    for (u32 i = 0; i < count; ++i) {
        Vtx_t *vertex = &batch->vertices[i].v;
        struct WorldPoint p = read_point(reader);
        if (!room_contains(room, p)) return false;
        vertex->ob[0] = p.x;
        vertex->ob[1] = p.y;
        vertex->ob[2] = p.z;
        vertex->tc[0] = read_s16(reader);
        vertex->tc[1] = read_s16(reader);
        memcpy(vertex->cn, read_bytes(reader, 4), 4);
    }
    return !reader->failed;
}

static bool read_room(struct SceneReader *reader, struct WorldSceneRoomDesc *desc,
                      struct FileRoom *room, bool legacy, bool materialFormat,
                      bool fogFormat, bool decalFormat) {
    desc->id = read_u32(reader);
    desc->min = read_point(reader);
    desc->max = read_point(reader);
    desc->neighborCount = read_u16(reader);
    u16 count = read_u16(reader); /* WSC1 render triangles, WSC2 batches */
    room->vertexCount = read_u16(reader);
    room->triangleCount = read_u16(reader);
    u16 stateCount = legacy ? 0 : read_u16(reader);
    room->batchCount = legacy ? 1 : count;
    if (reader->failed || desc->neighborCount > WORLD_SCENE_MAX_ROOM_NEIGHBORS
        || !count || (legacy && count > WORLD_SCENE_FILE_MAX_TRIANGLES)
        || room->batchCount > (decalFormat ? WORLD_SCENE_MAX_BATCHES_WSC5 : WORLD_SCENE_MAX_BATCHES_LEGACY)
        || stateCount > WORLD_SCENE_MAX_BATCHES_LEGACY || (materialFormat && stateCount)
        || !room->vertexCount || room->vertexCount > WORLD_REGION_MAX_VERTICES
        || !room->triangleCount || room->triangleCount > WORLD_REGION_MAX_TRIANGLES) return false;

    room->batches = calloc(room->batchCount, sizeof(*room->batches));
    if (!room->batches) return false;
    if (legacy) {
        struct FileBatch *batch = &room->batches[0];
        batch->triangleCount = count;
        batch->width = read_u16(reader);
        batch->height = read_u16(reader);
        if (!read_texture(reader, batch->texture, batch->width, batch->height, true, false)) return false;
    }
    desc->neighbors = room->neighbors;
    desc->userData = room;
    for (u32 i = 0; i < desc->neighborCount; ++i) room->neighbors[i] = read_u32(reader);

    if (stateCount) {
        room->states = calloc(stateCount, sizeof(*room->states));
        if (!room->states) return false;
        for (u16 i = 0; i < stateCount; ++i) {
            struct WorldSceneDrawState *state = &room->states[i];
            state->scrollS = read_s16(reader);
            state->scrollT = read_s16(reader);
            const u8 *tint = read_bytes(reader, sizeof(state->tint));
            if (!tint) return false;
            memcpy(state->tint, tint, sizeof(state->tint));
        }
    }

    u32 total = 0;
    for (u16 i = 0; i < room->batchCount; ++i) {
        struct FileBatch *batch = &room->batches[i];
        if (materialFormat) {
            batch->pass = read_u16(reader);
            batch->triangleCount = read_u16(reader);
            size_t materialBytesBefore = reader->remaining;
            if (batch->pass > 1 || !read_material(reader, batch, fogFormat, decalFormat)) {
                fprintf(stderr, "Scene room %u batch %u has invalid material (pass=%u triangles=%u bytesBefore=%zu bytesAfter=%zu)\n",
                        desc->id, i, batch->pass, batch->triangleCount,
                        materialBytesBefore, reader->remaining);
                WSC_DIAG_LOG("Scene material invalid room=%u batch=%u pass=%u triangles=%u bytesBefore=%zu bytesAfter=%zu",
                             desc->id, i, batch->pass, batch->triangleCount,
                             materialBytesBefore, reader->remaining);
                return false;
            }
        } else if (!legacy) {
            batch->pass = read_u16(reader);
            batch->state = read_u16(reader);
            batch->triangleCount = read_u16(reader);
            batch->width = read_u16(reader);
            batch->height = read_u16(reader);
            if (batch->pass > 1 || batch->state > stateCount
                || !read_texture(reader, batch->texture, batch->width, batch->height, false, false))
                return false;
        }
        total += batch->triangleCount;
        if (total > WORLD_SCENE_FILE_MAX_TRIANGLES) return false;
        size_t vertexOffset = reader->remaining;
        if (!read_render_vertices(reader, desc, batch)) {
            WSC_DIAG_LOG("Scene render batch invalid room=%u batch=%u triangleCount=%u bytesBefore=%zu bytesAfter=%zu",
                         desc->id, i, batch->triangleCount, vertexOffset, reader->remaining);
            return false;
        }
    }

    size_t payloadSize = 6u * room->vertexCount + 10u * room->triangleCount;
    if (reader->remaining < payloadSize) return false;
    room->collisionVertices = calloc(room->vertexCount, sizeof(*room->collisionVertices));
    room->collisionTriangles = calloc(room->triangleCount, sizeof(*room->collisionTriangles));
    if (!room->collisionVertices || !room->collisionTriangles) return false;
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
    return !reader->failed && build_display_lists(room, legacy);
}

static s32 load_scene(void *context, u32 id, struct WorldScenePackage *package) {
    char path[48];
    uint64_t size = 0;
    (void)context;
    u8 *bytes;
#ifdef IOS_SCENE_DIAGNOSTIC
    if (id == SCENE_DIAGNOSTIC_PACKAGE || id == SCENE_DIAGNOSTIC_WSC3_PACKAGE
        || id == SCENE_DIAGNOSTIC_INVALID_PACKAGE) {
        snprintf(path, sizeof(path), SCENE_DIAGNOSTIC_DIRECTORY "scenes/%u.wsc", id);
        bytes = fs_load_file_bounded(path, WORLD_SCENE_FILE_MAX_BYTES, &size);
    } else
#endif
    {
        snprintf(path, sizeof(path), "scenes/%u.wsc", id);
        bytes = fs_load_scene_resource(path, WORLD_SCENE_FILE_MAX_BYTES, &size);
    }
    if (!bytes) {
        if (fs_sys_file_exists(fs_get_write_path(path))) {
            fprintf(stderr, "Scene package %u is unreadable or exceeds the size limit\n", id);
            WSC_DIAG_LOG("WSC scene package unreadable or over size limit id=%u path=%s", id, path);
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
    bool legacy = magic && !memcmp(magic, "WSC1", 4);
    bool materialFormat = magic && (!memcmp(magic, "WSC3", 4) || !memcmp(magic, "WSC4", 4)
                                    || !memcmp(magic, "WSC5", 4));
    bool fogFormat = magic && (!memcmp(magic, "WSC4", 4) || !memcmp(magic, "WSC5", 4));
    bool decalFormat = magic && !memcmp(magic, "WSC5", 4);
    if (materialFormat && !gfx_supports_scene_materials()) {
        fprintf(stderr, "Scene package %u requires WSC3/WSC4/WSC5 render support\n", id);
        goto fail;
    }
#ifdef IOS_SCENE_DIAGNOSTIC
    if (id == SCENE_EXTERNAL_PACKAGE && !materialFormat) {
        fprintf(stderr, "External scene slot requires a WSC3, WSC4, or WSC5 package\n");
        goto fail;
    }
#endif
    if (!magic || (!legacy && !materialFormat && memcmp(magic, "WSC2", 4)) || fileId != id || reader.failed
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
        if (!read_room(&reader, &scene->descs[i], &scene->rooms[i], legacy,
                       materialFormat, fogFormat, decalFormat)) goto fail;
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
#ifdef IOS_SCENE_DIAGNOSTIC
    if (id == SCENE_EXTERNAL_PACKAGE) {
        char format[5] = {0};
        u32 batchCount = 0, renderTriangles = 0, collisionTriangles = 0, textureReferences = 0;
        memcpy(format, magic, 4);
        for (u32 r = 0; r < rooms; ++r) {
            const struct FileRoom *room = &scene->rooms[r];
            collisionTriangles += room->triangleCount;
            batchCount += room->batchCount;
            for (u16 b = 0; b < room->batchCount; ++b) {
                const struct FileBatch *batch = &room->batches[b];
                renderTriangles += batch->triangleCount;
                if (batch->texture[0]) ++textureReferences;
                if (batch->material) {
                    textureReferences += batch->material->texture[0].name[0] != '\0';
                    textureReferences += batch->material->texture[1].name[0] != '\0';
                }
            }
        }
        WSC_DIAG_LOG("External scene package parsed id=%u format=%s rooms=%u entrances=%u exits=%u batches=%u renderTriangles=%u collisionTriangles=%u textureReferences=%u bytes=%llu",
                     id, format, rooms, entrances, exits, batchCount, renderTriangles,
                     collisionTriangles, textureReferences, (unsigned long long)size);
    }
#endif
    *package = (struct WorldScenePackage){id, scene->descs, rooms, scene->entrances,
                                        entrances, scene->exits, exits, scene};
    free(bytes);
    return 1;

fail:
    fprintf(stderr, "Scene package %u has invalid data near byte %zu\n",
            id, (size_t)(reader.next - bytes));
    WSC_DIAG_LOG("Scene package rejected id=%u format=%s byteOffset=%zu remaining=%zu",
                 id, magic && !memcmp(magic, "WSC5", 4) ? "WSC5"
                    : magic && !memcmp(magic, "WSC4", 4) ? "WSC4"
                    : magic && !memcmp(magic, "WSC3", 4) ? "WSC3"
                    : magic && !memcmp(magic, "WSC2", 4) ? "WSC2" : "unknown",
                 (size_t)(reader.next - bytes), reader.remaining);
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
        .renderList = room->commands[0], .translucentList = room->commands[1],
        .owner = room, .externallyManaged = 1,
    };
    enum WorldRegionResult result = world_region_register(&region);
    if (result != WORLD_REGION_OK) {
        fprintf(stderr, "Scene package %u room %u collision registration failed (%d)\n",
                package->id, desc->id, result);
        WSC_DIAG_LOG("Scene collision registration failed package=%u room=%u result=%d",
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
    invalidate_room_textures(room);
}

const struct WorldSceneProvider gWorldSceneFileProvider = {
    load_scene, release_scene, activate_room, deactivate_room,
};
#endif
