#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/pc/gfx/gfx_texture_cache_util.h"

struct TextureHashmapNode {
    uint32_t key;
    uint32_t texture_id;
    struct TextureHashmapNode *next;
};

static int lookup(struct TextureHashmapNode **buckets, size_t bucket,
                  struct TextureHashmapNode *pool, uint32_t pool_pos, uint32_t key) {
    struct TextureHashmapNode **node = &buckets[bucket];
    while (*node != NULL && *node >= pool && *node < pool + pool_pos) {
        if ((*node)->key == key) return (int)(*node)->texture_id;
        node = &(*node)->next;
    }
    return 0;
}

int main(void) {
    struct TextureHashmapNode pool[3] = {
        {.key = 0x1111, .texture_id = 7, .next = NULL},
        {.key = 0x2222, .texture_id = 9, .next = NULL},
        {.key = 0, .texture_id = 0, .next = NULL},
    };
    struct TextureHashmapNode *buckets[4] = {&pool[0], &pool[1], NULL, NULL};
    struct TextureHashmapNode *active_tiles[2] = {&pool[0], &pool[1]};
    uint32_t pool_pos = 2;

    assert(lookup(buckets, 0, pool, pool_pos, 0x1111) == 7);
    assert(lookup(buckets, 1, pool, pool_pos, 0x2222) == 9);

    /* The previous implementation rewound pool_pos but left both hash roots
     * and active tile bindings pointing into the slots about to be reused. */
    assert(gfx_texture_cache_reset_generation(buckets, 4, &pool_pos, active_tiles, 2));
    assert(pool_pos == 0);
    for (size_t i = 0; i < 4; i++) assert(buckets[i] == NULL);
    assert(active_tiles[0] == NULL && active_tiles[1] == NULL);

    pool[0] = (struct TextureHashmapNode){.key = 0x3333, .texture_id = 12, .next = NULL};
    buckets[2] = &pool[0];
    active_tiles[0] = &pool[0];
    pool_pos = 1;
    assert(lookup(buckets, 0, pool, pool_pos, 0x1111) == 0);
    assert(lookup(buckets, 1, pool, pool_pos, 0x2222) == 0);
    assert(lookup(buckets, 2, pool, pool_pos, 0x3333) == 12);

    assert(!gfx_texture_cache_reset_generation(NULL, 4, &pool_pos, active_tiles, 2));
    assert(!gfx_texture_cache_reset_generation(buckets, 4, NULL, active_tiles, 2));
    assert(!gfx_texture_cache_reset_generation(buckets, 4, &pool_pos, NULL, 2));
    puts("texture cache generation reset: passed");
    return 0;
}
