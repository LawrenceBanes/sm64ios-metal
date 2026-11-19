#ifndef SM64IOS_DYNOS_ACTOR_H
#define SM64IOS_DYNOS_ACTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parsed DynOS actor data is owned by this bundle. Offsets refer to the
 * decompressed byte stream and remain valid until dynos_actor_free(). The
 * decoder never turns serialized pointers into executable native pointers. */
typedef struct {
    uint8_t type;
    char name[255];
    size_t data_offset;
    size_t data_size;
    uint32_t count;
    uint32_t resolved_alias;
    bool float_vertices;
    size_t *word_offsets; /* Gfx/GeoLayout words, excluding pointer sidecars */
} DynosActorRecord;

typedef struct {
    uint32_t owner;
    uint32_t word;
    uint32_t target;
    uint32_t aux;
    uint8_t kind; /* 1 named pointer, 2 built-in function, 3 Lua variable */
    char name[255];
} DynosActorReference;

typedef struct {
    uint8_t *data;
    size_t size;
    DynosActorRecord *records;
    size_t record_count;
    DynosActorReference *references;
    size_t reference_count;
} DynosActorBundle;

bool dynos_actor_decode(const uint8_t *file, size_t file_size,
                        DynosActorBundle *out, char *error, size_t error_size);
void dynos_actor_free(DynosActorBundle *bundle);
const DynosActorRecord *dynos_actor_find(const DynosActorBundle *bundle,
                                         uint8_t type, const char *name);

#endif
