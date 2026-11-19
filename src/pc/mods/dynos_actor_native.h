#ifndef SM64IOS_DYNOS_ACTOR_NATIVE_H
#define SM64IOS_DYNOS_ACTOR_NATIVE_H

#include "dynos_actor.h"

typedef const void *(*DynosTextureResolver)(void *context, const char *name,
                                             const uint8_t *png, size_t size);
typedef const void *(*DynosFunctionResolver)(void *context, uint32_t index);

/* Materializes the decoded actor's native Vtx, Gfx, and GeoLayout arrays.
 * The caller owns texture/function callback results. Native arrays remain
 * owned by this object and must outlive every graph node using them. */
typedef struct {
    void **resources;
    bool *owned;
    size_t count;
} DynosNativeActor;

bool dynos_actor_native_build(const DynosActorBundle *bundle,
                              DynosTextureResolver texture_resolver,
                              DynosFunctionResolver function_resolver,
                              void *context, DynosNativeActor *out,
                              char *error, size_t error_size);
void dynos_actor_native_free(DynosNativeActor *actor);
const void *dynos_actor_native_find(const DynosActorBundle *bundle,
                                    const DynosNativeActor *actor,
                                    uint8_t type, const char *name);
bool dynos_actor_native_validate_root_geo(const DynosActorBundle *bundle,
                                          const DynosNativeActor *actor,
                                          const char *name,
                                          char *error, size_t error_size);

#endif
