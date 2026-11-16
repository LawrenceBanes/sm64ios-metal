#ifndef SURFACE_PARTITION_H
#define SURFACE_PARTITION_H

#include <PR/ultratypes.h>

#if defined(IOS_HYRULE_WORLD) || defined(IOS_CONNECTED_WORLD_TEST)
#define IOS_EXPANDED_WORLD_COLLISION 1
/* OoT spot00 reaches Z=16529; the local seamless test also crosses the stock
 * ±8192 boundary. Keep coordinates in SM64 s16 and widen only opt-in worlds. */
#define LEVEL_BOUNDARY_MAX 0x8000
#define SURFACE_PARTITION_DIM 64
#else
#define LEVEL_BOUNDARY_MAX 0x2000
#define SURFACE_PARTITION_DIM 16
#endif

#define CELL_SIZE 0x400
#define SURFACE_PARTITION_MASK (SURFACE_PARTITION_DIM - 1)

static inline s32 surface_partition_cell_index(s32 coord) {
    return (coord + LEVEL_BOUNDARY_MAX) / CELL_SIZE;
}

static inline s32 surface_partition_coord_in_bounds(s32 coord) {
#if defined(IOS_EXPANDED_WORLD_COLLISION)
    /* The importer stores coordinates as s16, so -32768 is representable. */
    return coord >= -LEVEL_BOUNDARY_MAX && coord < LEVEL_BOUNDARY_MAX;
#else
    /* Preserve the stock game's open boundary on both sides. */
    return coord > -LEVEL_BOUNDARY_MAX && coord < LEVEL_BOUNDARY_MAX;
#endif
}

/* Check before the legacy f32 -> s16 cast. Connected worlds must never wrap
 * a distant coordinate into an unrelated cell (stock PU behavior is retained). */
static inline s32 surface_partition_float_in_bounds(f32 coord) {
    return coord >= -LEVEL_BOUNDARY_MAX && coord < LEVEL_BOUNDARY_MAX;
}

#endif
