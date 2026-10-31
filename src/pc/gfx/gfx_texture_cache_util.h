#ifndef GFX_TEXTURE_CACHE_UTIL_H
#define GFX_TEXTURE_CACHE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct TextureHashmapNode;

/* Invalidate hash roots and active tile references before reusing arena nodes
 * and their renderer texture IDs. */
static inline bool gfx_texture_cache_reset_generation(
    struct TextureHashmapNode **buckets, size_t bucket_count, uint32_t *pool_position,
    struct TextureHashmapNode **active_tiles, size_t active_tile_count) {
    if (!buckets || !pool_position || (active_tile_count && !active_tiles) ||
        bucket_count > SIZE_MAX / sizeof(void *))
        return false;
    for (size_t i = 0; i < bucket_count; i++) buckets[i] = NULL;
    for (size_t i = 0; i < active_tile_count; i++) active_tiles[i] = NULL;
    *pool_position = 0;
    return true;
}

#endif
