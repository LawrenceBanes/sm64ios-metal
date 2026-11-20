#include "dynos_actor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinfl.h>

#define DYNOS_MAX_FILE (8u * 1024u * 1024u)
#define DYNOS_MAX_DECODED (16u * 1024u * 1024u)
#define DYNOS_MAX_RECORDS 4096u
#define DYNOS_MAX_REFERENCES 32768u
#define DYNOS_MAX_WORDS 262144u
#define DYNOS_MAX_TEXTURE (4u * 1024u * 1024u)

typedef struct {
    DynosActorBundle *bundle;
    size_t cursor;
    char *error;
    size_t error_size;
} Reader;

static bool fail(Reader *r, const char *message) {
    if (r->error && r->error_size)
        snprintf(r->error, r->error_size, "DynOS actor offset %zu: %s", r->cursor, message);
    return false;
}

static bool take(Reader *r, size_t count, const uint8_t **data) {
    if (count > r->bundle->size - r->cursor) return fail(r, "truncated record");
    if (data) *data = r->bundle->data + r->cursor;
    r->cursor += count;
    return true;
}

static bool read_u8(Reader *r, uint8_t *value) {
    const uint8_t *p;
    if (!take(r, 1, &p)) return false;
    *value = *p;
    return true;
}

static bool read_u32(Reader *r, uint32_t *value) {
    const uint8_t *p;
    if (!take(r, 4, &p)) return false;
    *value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return true;
}

static bool read_name(Reader *r, char name[255]) {
    uint8_t count;
    const uint8_t *p;
    if (!read_u8(r, &count) || !count || count > 254 || !take(r, count, &p))
        return fail(r, "invalid or truncated name");
    if (memchr(p, 0, count)) return fail(r, "name contains NUL");
    memcpy(name, p, count);
    name[count] = 0;
    return true;
}

static bool add_reference(Reader *r, uint32_t owner, uint32_t word, uint32_t value) {
    uint8_t kind = value == 0x52544e50u ? 1 : value == 0x434e5546u ? 2 :
                   value == 0x5641554cu ? 3 : 0;
    if (!kind) return true;
    DynosActorBundle *b = r->bundle;
    if (b->reference_count >= DYNOS_MAX_REFERENCES) return fail(r, "too many references");
    DynosActorReference *refs = realloc(b->references,
                                        (b->reference_count + 1) * sizeof(*refs));
    if (!refs) return fail(r, "out of memory for references");
    b->references = refs;
    DynosActorReference *ref = &refs[b->reference_count++];
    memset(ref, 0, sizeof(*ref));
    ref->owner = owner;
    ref->word = word;
    ref->kind = kind;
    if (kind == 2) return read_u32(r, &ref->aux);
    if (!read_name(r, ref->name)) return false;
    return kind != 1 || read_u32(r, &ref->aux);
}

static bool read_record(Reader *r, uint32_t record_index) {
    DynosActorRecord *rec = &r->bundle->records[record_index];
    uint32_t count;
    rec->resolved_alias = UINT32_MAX;
    if (!read_u8(r, &rec->type)) return false;
    if (rec->type == 0) return fail(r, "unexpected terminator");
    if (rec->type != 9 && !read_name(r, rec->name)) return false;
    rec->data_offset = r->cursor;
    switch (rec->type) {
        case 1: case 24: /* Lights1 / Lights0: Ambient + Light */
            if (!take(r, 24, NULL)) return false;
            break;
        case 18: /* Light_t */
            if (!take(r, 16, NULL)) return false;
            break;
        case 19: /* Ambient_t */
            if (!take(r, 8, NULL)) return false;
            break;
        case 2: { /* embedded PNG or reference to a prior texture */
            if (!read_u32(r, &count)) return false;
            if (count == 0x52584554u) {
                char alias[255];
                if (!read_name(r, alias)) return false;
                rec->resolved_alias = UINT32_MAX;
                for (uint32_t i = 0; i < record_index; ++i)
                    if (r->bundle->records[i].type == 2 &&
                        !strcmp(r->bundle->records[i].name, alias)) {
                        rec->resolved_alias = i;
                        break;
                    }
                if (rec->resolved_alias == UINT32_MAX)
                    return fail(r, "texture alias has no earlier target");
            } else {
                if (count > DYNOS_MAX_TEXTURE) return fail(r, "texture exceeds byte limit");
                rec->count = count;
                if (!take(r, count, NULL)) return false;
                if (count && (count < 8 || memcmp(r->bundle->data + rec->data_offset + 4,
                                                   "\x89PNG\r\n\x1a\n", 8)))
                    return fail(r, "embedded texture is not PNG");
            }
            break;
        }
        case 3: { /* packed N64 Vtx, optionally followed by float vertices */
            if (!read_u32(r, &count) || count > DYNOS_MAX_WORDS)
                return fail(r, "invalid vertex count");
            rec->count = count;
            if (count) {
                const uint8_t *first;
                if (!take(r, 16, &first)) return false;
                rec->float_vertices = !memcmp(first, "\x46\x33\x32\x56\x54\x58", 6);
                if (rec->float_vertices) {
                    rec->count = count - 1;
                    if (!take(r, (size_t)(count - 1) * 22u, NULL)) return false;
                } else if (!take(r, (size_t)(count - 1) * 16u, NULL)) return false;
            }
            break;
        }
        case 4: case 5: case 20: { /* Gfx, GeoLayout, texture pointer list */
            if (!read_u32(r, &count) || count > DYNOS_MAX_WORDS)
                return fail(r, "invalid command count");
            rec->count = count;
            if ((size_t)count > (r->bundle->size - r->cursor) / (rec->type == 4 ? 8u : 4u))
                return fail(r, "command count exceeds remaining bytes");
            rec->word_offsets = calloc(count ? count : 1, sizeof(*rec->word_offsets));
            if (!rec->word_offsets) return fail(r, "out of memory for command offsets");
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t first, value;
                rec->word_offsets[i] = r->cursor;
                if (!read_u32(r, &first)) return false;
                value = first;
                if (rec->type == 4 && !read_u32(r, &value)) return false;
                if (!add_reference(r, record_index, i, value)) return false;
            }
            break;
        }
        case 8: { /* Animation header + values + indices */
            if (!take(r, 12, NULL) || !read_u32(r, &count)) return false;
            rec->count = count; /* frame length */
            for (int i = 0; i < 2; ++i) {
                uint32_t entries;
                if (!read_u32(r, &entries) || entries > DYNOS_MAX_WORDS)
                    return fail(r, "invalid animation array length");
                if (!take(r, (size_t)entries * 2u, NULL)) return false;
            }
            break;
        }
        case 9: /* Animation table entry is only a name. */
            if (!read_name(r, rec->name)) return false;
            break;
        default: {
            char message[96];
            snprintf(message, sizeof(message), "unsupported record type %u", rec->type);
            return fail(r, message);
        }
    }
    rec->data_size = r->cursor - rec->data_offset;
    return true;
}

static bool resolve_references(Reader *r) {
    DynosActorBundle *b = r->bundle;
    for (size_t i = 0; i < b->reference_count; ++i) {
        DynosActorReference *ref = &b->references[i];
        if (ref->kind != 1) continue;
        ref->target = UINT32_MAX;
        for (size_t j = 0; j < b->record_count; ++j)
            if (!strcmp(b->records[j].name, ref->name)) {
                ref->target = (uint32_t)j;
                break;
            }
        if (ref->target == UINT32_MAX) {
            char message[320];
            snprintf(message, sizeof(message), "unresolved pointer '%.120s' in '%.120s'",
                     ref->name, b->records[ref->owner].name);
            return fail(r, message);
        }
    }
    return true;
}

/* G_DL and GeoLayout branches are recursive in the native engine. Reject
 * cycles before any parsed pointers can be handed to the renderer. */
static bool visit_links(Reader *r, uint32_t index, uint8_t *state, unsigned depth) {
    if (depth > 128) return fail(r, "display-list/geolayout reference depth exceeds limit");
    if (state[index] == 2) return true;
    if (state[index] == 1) return fail(r, "cyclic display-list/geolayout reference");
    state[index] = 1;
    for (size_t i = 0; i < r->bundle->reference_count; ++i) {
        const DynosActorReference *ref = &r->bundle->references[i];
        if (ref->kind != 1 || ref->owner != index) continue;
        uint8_t type = r->bundle->records[ref->target].type;
        if ((type == 4 || type == 5) &&
            !visit_links(r, ref->target, state, depth + 1)) return false;
    }
    state[index] = 2;
    return true;
}

static bool reject_cycles(Reader *r) {
    uint8_t *state = calloc(r->bundle->record_count, 1);
    if (!state) return fail(r, "out of memory checking references");
    bool valid = true;
    for (size_t i = 0; i < r->bundle->record_count; ++i) {
        uint8_t type = r->bundle->records[i].type;
        if ((type == 4 || type == 5) && !visit_links(r, (uint32_t)i, state, 0)) {
            valid = false;
            break;
        }
    }
    free(state);
    return valid;
}

void dynos_actor_free(DynosActorBundle *bundle) {
    if (!bundle) return;
    free(bundle->data);
    for (size_t i = 0; i < bundle->record_count; ++i)
        free(bundle->records[i].word_offsets);
    free(bundle->records);
    free(bundle->references);
    memset(bundle, 0, sizeof(*bundle));
}

const DynosActorRecord *dynos_actor_find(const DynosActorBundle *bundle,
                                         uint8_t type, const char *name) {
    if (!bundle || !name) return NULL;
    for (size_t i = 0; i < bundle->record_count; ++i)
        if (bundle->records[i].type == type && !strcmp(bundle->records[i].name, name))
            return &bundle->records[i];
    return NULL;
}

bool dynos_actor_decode(const uint8_t *file, size_t file_size,
                        DynosActorBundle *out, char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    Reader r = { out, 0, error, error_size };
    if (!file || file_size < 18 || file_size > DYNOS_MAX_FILE ||
        memcmp(file, "DYNOSBIN", 8)) return fail(&r, "invalid header or file size");
    uint64_t decoded_size = 0;
    for (unsigned i = 0; i < 8; ++i) decoded_size |= (uint64_t)file[8 + i] << (8u * i);
    if (!decoded_size || decoded_size > DYNOS_MAX_DECODED)
        return fail(&r, "decoded size exceeds limit");
    out->data = malloc((size_t)decoded_size);
    if (!out->data) return fail(&r, "out of memory for decompression");
    out->size = (size_t)decoded_size;
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (unsigned char *)file + 16;
    stream.avail_in = (unsigned)(file_size - 16);
    stream.next_out = out->data;
    stream.avail_out = (unsigned)out->size;
    int status = mz_inflateInit2(&stream, MAX_WBITS);
    if (status == MZ_OK) {
        status = mz_inflate(&stream, MZ_FINISH);
        mz_inflateEnd(&stream);
    }
    if (status != MZ_STREAM_END || stream.total_out != out->size ||
        stream.total_in != file_size - 16) {
        fail(&r, "invalid zlib stream or decoded length");
        dynos_actor_free(out);
        return false;
    }
    out->records = calloc(DYNOS_MAX_RECORDS, sizeof(*out->records));
    if (!out->records) {
        fail(&r, "out of memory for records");
        dynos_actor_free(out);
        return false;
    }
    while (r.cursor < out->size) {
        if (out->record_count >= DYNOS_MAX_RECORDS ||
            !read_record(&r, (uint32_t)out->record_count)) {
            if (out->record_count >= DYNOS_MAX_RECORDS) fail(&r, "too many records");
            /* read_record may already have allocated this incomplete entry. */
            free(out->records[out->record_count].word_offsets);
            dynos_actor_free(out);
            return false;
        }
        ++out->record_count;
    }
    if (!out->record_count || !resolve_references(&r) || !reject_cycles(&r)) {
        if (!out->record_count) fail(&r, "empty actor");
        dynos_actor_free(out);
        return false;
    }
    return true;
}
