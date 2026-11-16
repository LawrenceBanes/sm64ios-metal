#ifndef SURFACE_PARTITION_H
#define SURFACE_PARTITION_H

#include <PR/ultratypes.h>

#if defined(IOS_HYRULE_WORLD)
/* OoT spot00 collision reaches Z=16529. Keep source coordinates in SM64's
 * signed-16 collision format and widen only the opt-in world partition. */
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
#if defined(IOS_HYRULE_WORLD)
    /* The importer stores coordinates as s16, so -32768 is representable. */
    return coord >= -LEVEL_BOUNDARY_MAX && coord < LEVEL_BOUNDARY_MAX;
#else
    /* Preserve the stock game's open boundary on both sides. */
    return coord > -LEVEL_BOUNDARY_MAX && coord < LEVEL_BOUNDARY_MAX;
#endif
}

#endif
