#include "dynos_actor_native.h"

#include "sm64.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool errorf(char *error, size_t size, const char *message, const char *name) {
    if (error && size) snprintf(error, size, "%s: %.120s", message, name ? name : "?");
    return false;
}

void dynos_actor_native_free(DynosNativeActor *actor) {
    if (!actor) return;
    for (size_t i = 0; i < actor->count; ++i)
        if (actor->owned && actor->owned[i]) free(actor->resources[i]);
    free(actor->resources);
    free(actor->owned);
    memset(actor, 0, sizeof(*actor));
}

const void *dynos_actor_native_find(const DynosActorBundle *bundle,
                                    const DynosNativeActor *actor,
                                    uint8_t type, const char *name) {
    if (!bundle || !actor) return NULL;
    for (size_t i = 0; i < bundle->record_count && i < actor->count; ++i)
        if (bundle->records[i].type == type && !strcmp(bundle->records[i].name, name))
            return actor->resources[i];
    return NULL;
}

/* The engine's GeoLayout interpreter has no step budget. Admit only bounded
 * command streams with balanced node nesting and acyclic references before
 * passing mod data to process_geo_layout(). Review new opcodes against
 * geo_layout.c before adding them here. */
static bool validate_geo_record(const DynosActorBundle *bundle,
                                const DynosNativeActor *actor,
                                const DynosActorRecord *rec, bool require_root,
                                char *error, size_t error_size) {
    const char *name = rec->name;
    if (!rec->count) return errorf(error, error_size, "empty geolayout", name);
    if (rec->count > 256)
        return errorf(error, error_size, "geolayout exceeds 256-word graph budget", name);
    size_t index = (size_t)(rec - bundle->records);
    if (index >= actor->count || !actor->resources[index])
        return errorf(error, error_size, "geolayout is not linked", name);
    const GeoLayout *words = actor->resources[index];
    unsigned depth = 0;
    bool has_node_root = false;
    for (uint32_t cursor = 0; cursor < rec->count;) {
        uint32_t command = (uint32_t)words[cursor];
        uint8_t opcode = (uint8_t)command;
        uint8_t params = (uint8_t)(command >> 8);
        uint32_t width = 0;
        switch (opcode) {
            case 0x01: /* GEO_END */
                if (cursor != rec->count - 1 || depth || (require_root && !has_node_root))
                    return errorf(error, error_size, "unbalanced geolayout end", name);
                return true;
            case 0x00: /* GEO_BRANCH_AND_LINK: target pointer is a separate word. */
            case 0x02: /* GEO_BRANCH */
                width = 2; break;
            case 0x03: /* GEO_RETURN */
                if (cursor != rec->count - 1 || depth || (require_root && !has_node_root))
                    return errorf(error, error_size, "unbalanced geolayout return", name);
                return true;
            case 0x04: /* GEO_OPEN_NODE */
                if (++depth > 30) return errorf(error, error_size, "geolayout nesting exceeds limit", name);
                width = 1; break;
            case 0x05: /* GEO_CLOSE_NODE */
                if (!depth) return errorf(error, error_size, "geolayout close underflows", name);
                --depth; width = 1; break;
            case 0x0b: /* GEO_NODE_START */
                has_node_root = true;
                width = 1; break;
            case 0x20: /* GEO_CULLING_RADIUS */
            case 0x17: /* GEO_RENDER_OBJ */
            case 0x1b: /* GEO_COPY_VIEW */
                width = 1; break;
            case 0x0d: /* GEO_RENDER_RANGE */
            case 0x0e: /* GEO_SWITCH_CASE */
            case 0x15: /* GEO_DISPLAY_LIST */
            case 0x16: /* GEO_SHADOW */
            case 0x18: /* GEO_ASM */
                width = 2; break;
            case 0x10: { /* GEO_TRANSLATE_ROTATE family. */
                unsigned layout = (params & 0x70u) >> 4;
                static const uint8_t words_by_layout[] = { 4, 2, 2, 1 };
                if (layout >= sizeof(words_by_layout))
                    return errorf(error, error_size, "invalid GEO_TRANSLATE_ROTATE layout", name);
                width = words_by_layout[layout] + !!(params & 0x80u);
                break;
            }
            case 0x11: /* GEO_TRANSLATE_NODE */
            case 0x12: /* GEO_ROTATION_NODE */
            case 0x14: /* GEO_BILLBOARD_WITH_PARAMS */
                width = 2 + !!(params & 0x80); break;
            case 0x13: /* GEO_ANIMATED_PART */
            case 0x1c: /* GEO_HELD_OBJECT */
                width = 3; break;
            case 0x1d: /* GEO_SCALE */
                width = 2 + !!(params & 0x80); break;
            case 0x1a: /* GEO_NOP_1A */
            case 0x1e: /* GEO_NOP_1E */
                width = 2; break;
            case 0x1f: /* GEO_NOP_1F */
                width = 4; break;
            default: {
                char message[100];
                snprintf(message, sizeof(message), "unsupported GeoLayout opcode 0x%02x", opcode);
                return errorf(error, error_size, message, name);
            }
        }
        if (width > rec->count - cursor)
            return errorf(error, error_size, "truncated GeoLayout command", name);
        cursor += width;
    }
    return errorf(error, error_size, "geolayout has no GEO_END", name);
}

bool dynos_actor_native_validate_root_geo(const DynosActorBundle *bundle,
                                          const DynosNativeActor *actor,
                                          const char *name,
                                          char *error, size_t error_size) {
    const DynosActorRecord *root = dynos_actor_find(bundle, 5, name);
    if (!root || !actor) return errorf(error, error_size, "geolayout not found", name);
    /* Every linked GeoLayout can be reached through a branch. Validate every
     * command stream before the engine's unbounded graph interpreter sees it. */
    for (size_t i = 0; i < bundle->record_count; ++i) {
        const DynosActorRecord *rec = &bundle->records[i];
        if (rec->type == 5 &&
            !validate_geo_record(bundle, actor, rec, rec == root, error, error_size))
            return false;
    }
    return true;
}

static bool vertex_array(const DynosActorBundle *bundle, const DynosActorRecord *rec,
                         void **result) {
    if (!rec->count) return false;
    Vtx *vertices = calloc(rec->count, sizeof(*vertices));
    if (!vertices) return false;
    const uint8_t *p = bundle->data + rec->data_offset + 4;
    if (rec->float_vertices) p += 16; /* upstream's F32VTX sentinel */
    for (uint32_t i = 0; i < rec->count; ++i) {
        Vtx_t *v = &vertices[i].v;
        if (rec->float_vertices) {
            for (int j = 0; j < 3; ++j) {
                uint32_t bits = u32le(p + j * 4);
                memcpy(&v->ob[j], &bits, sizeof(bits));
            }
            p += 12;
        } else {
            for (int j = 0; j < 3; ++j) v->ob[j] = (float)(int16_t)u16le(p + j * 2);
            p += 6;
        }
        v->flag = u16le(p); p += 2;
        for (int j = 0; j < 2; ++j) v->tc[j] = (int16_t)u16le(p + j * 2);
        p += 4;
        memcpy(v->cn, p, 4); p += 4;
    }
    *result = vertices;
    return true;
}

static const void *reference_target(const DynosActorBundle *bundle,
                                    const DynosNativeActor *actor,
                                    const DynosActorReference *ref) {
    if (ref->target >= actor->count) return NULL;
    const DynosActorRecord *rec = &bundle->records[ref->target];
    const uint8_t *base = actor->resources[ref->target];
    if (!base) return NULL;
    switch (rec->type) {
        case 1: case 24:
            if (ref->aux == 1) return base + 8; /* Lights1.l[0] */
            if (ref->aux == 2) return base;     /* Lights1.a */
            return NULL;
        case 18:
            if (ref->aux == 1 || ref->aux == 2) return base + (ref->aux == 2 ? 4 : 0);
            if (ref->aux == 3) return base + 8;
            return NULL;
        case 19:
            if (ref->aux == 1 || ref->aux == 2) return base + (ref->aux == 2 ? 4 : 0);
            return NULL;
        case 3:
            return ref->aux < rec->count ? base + (size_t)ref->aux * sizeof(Vtx) : NULL;
        case 4:
            return ref->aux < rec->count ? base + (size_t)ref->aux * sizeof(Gfx) : NULL;
        case 5:
            return ref->aux < rec->count ? base + (size_t)ref->aux * sizeof(GeoLayout) : NULL;
        case 2:
            /* Texture records are renderer handles, not byte-addressable
             * source buffers. Upstream pointer offsets cannot be applied. */
            return ref->aux == 0 ? base : NULL;
        default:
            return NULL;
    }
}

bool dynos_actor_native_build(const DynosActorBundle *bundle,
                              DynosTextureResolver texture_resolver,
                              DynosFunctionResolver function_resolver,
                              void *context, DynosNativeActor *out,
                              char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (!bundle || !out || !bundle->record_count) return false;
    memset(out, 0, sizeof(*out));
    out->resources = calloc(bundle->record_count, sizeof(*out->resources));
    if (!out->resources) return errorf(error, error_size, "out of memory", "resources");
    out->count = bundle->record_count;
    out->owned = calloc(bundle->record_count, sizeof(*out->owned));
    if (!out->owned) {
        errorf(error, error_size, "out of memory", "ownership");
        goto failed;
    }
    for (size_t i = 0; i < out->count; ++i) {
        const DynosActorRecord *rec = &bundle->records[i];
        const uint8_t *data = bundle->data + rec->data_offset;
        switch (rec->type) {
            case 1: case 24: case 18: case 19:
                out->resources[i] = malloc(rec->data_size);
                if (out->resources[i]) memcpy(out->resources[i], data, rec->data_size);
                out->owned[i] = true;
                break;
            case 2:
                if (rec->resolved_alias < i) {
                    /* Alias points to a callback-owned texture; it is not freed here. */
                    out->resources[i] = out->resources[rec->resolved_alias];
                } else if (rec->count) {
                    /* The callback registers PNG bytes in the renderer's texture pool. */
                    const void *texture = texture_resolver ?
                        texture_resolver(context, rec->name, data + 4, rec->count) : NULL;
                    out->resources[i] = (void *)texture;
                }
                if (!out->resources[i]) goto fail;
                break;
            case 3:
                if (!vertex_array(bundle, rec, &out->resources[i])) goto fail;
                out->owned[i] = true;
                break;
            case 4:
                out->resources[i] = calloc(rec->count, sizeof(Gfx));
                out->owned[i] = true;
                break;
            case 5:
                out->resources[i] = calloc(rec->count, sizeof(GeoLayout));
                out->owned[i] = true;
                break;
            default:
                errorf(error, error_size, "native activation does not support record type", rec->name);
                goto failed;
        }
        if (!out->resources[i]) goto fail;
        continue;
fail:
        errorf(error, error_size, "could not materialize record", rec->name);
        goto failed;
    }

    /* Populate native words only after every target has storage, so forward
     * references remain valid without relying on record order. */
    for (size_t i = 0; i < out->count; ++i) {
        const DynosActorRecord *rec = &bundle->records[i];
        if (rec->type == 4) {
            Gfx *commands = out->resources[i];
            for (uint32_t word = 0; word < rec->count; ++word) {
                const uint8_t *p = bundle->data + rec->word_offsets[word];
                commands[word].words.w0 = u32le(p);
                commands[word].words.w1 = u32le(p + 4);
            }
            if (!rec->count || (uint8_t)(commands[rec->count - 1].words.w0 >> 24) != G_ENDDL) {
                errorf(error, error_size, "display list lacks G_ENDDL", rec->name);
                goto failed;
            }
        } else if (rec->type == 5) {
            GeoLayout *words = out->resources[i];
            for (uint32_t word = 0; word < rec->count; ++word)
                words[word] = u32le(bundle->data + rec->word_offsets[word]);
        }
    }
    for (size_t i = 0; i < bundle->reference_count; ++i) {
        const DynosActorReference *ref = &bundle->references[i];
        const DynosActorRecord *owner = &bundle->records[ref->owner];
        const void *target = NULL;
        if (owner->type == 4 && ref->kind == 1) {
            uint8_t opcode = (uint8_t)(((Gfx *)out->resources[ref->owner])[ref->word].words.w0 >> 24);
            uint8_t target_type = bundle->records[ref->target].type;
            bool valid = (opcode == G_VTX && target_type == 3) ||
                         (opcode == G_DL && target_type == 4) ||
                         (opcode == G_SETTIMG && target_type == 2) ||
                         (opcode == G_MOVEMEM && (target_type == 1 || target_type == 24 ||
                                                  target_type == 18 || target_type == 19));
            if (!valid) {
                errorf(error, error_size, "display-list pointer type is unsupported", owner->name);
                goto failed;
            }
        }
        if (ref->kind == 1) target = reference_target(bundle, out, ref);
        else if (ref->kind == 2 && owner->type == 5 && function_resolver)
            target = function_resolver(context, ref->aux);
        if (!target) {
            errorf(error, error_size, "unresolved native pointer/function", owner->name);
            goto failed;
        }
        if (owner->type == 4) ((Gfx *)out->resources[ref->owner])[ref->word].words.w1 = (uintptr_t)target;
        else if (owner->type == 5) ((GeoLayout *)out->resources[ref->owner])[ref->word] = (uintptr_t)target;
        else {
            errorf(error, error_size, "unsupported pointer owner", owner->name);
            goto failed;
        }
    }
    return true;
failed:
    dynos_actor_native_free(out);
    return false;
}
