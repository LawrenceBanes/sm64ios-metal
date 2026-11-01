#include "pc/gfx/gfx_texture_override.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char path[256];
    assert(gfx_hd_texture_override_path("actors/goomba/goomba_face.rgba16", path, sizeof(path)));
    assert(strcmp(path, "hd_textures/gfx/actors/goomba/goomba_face.rgba16.png") == 0);
    assert(gfx_hd_texture_override_path("levels/bob/0.rgba16", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("../outside", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("levels/../../outside", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("/absolute/path", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("levels\\bob\\0.rgba16", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("levels//bob", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("levels/./bob", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("", path, sizeof(path)));
    assert(!gfx_hd_texture_override_path("levels/bob/0.rgba16", path, 8));

    assert(gfx_hd_texture_dimensions_compatible(32, 32, 512, 512));
    assert(gfx_hd_texture_dimensions_compatible(64, 32, 512, 256));
    assert(gfx_hd_texture_dimensions_compatible(32, 16, 512, 256));
    assert(!gfx_hd_texture_dimensions_compatible(32, 32, 512, 256));
    assert(!gfx_hd_texture_dimensions_compatible(64, 32, 32, 16));
    assert(!gfx_hd_texture_dimensions_compatible(0, 32, 512, 256));

    const uint64_t budget = 256ull * 1024ull * 1024ull;
    assert(gfx_hd_texture_budget_allows(100, 40, 80, 256));
    assert(!gfx_hd_texture_budget_allows(220, 0, 40, 256));
    assert(!gfx_hd_texture_budget_allows(100, 101, 1, 256));
    assert(!gfx_hd_texture_budget_allows(UINT64_MAX, 0, 1, budget));
    assert(gfx_hd_texture_budget_allows(budget, 1024, 1024, budget));
    assert(!gfx_hd_texture_budget_allows(budget, 0, 1, budget));

    puts("HD texture override path, dimension, and memory-budget checks passed");
    return 0;
}
