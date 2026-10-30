#include "pc/gfx/gfx_metal_state.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(gfx_metal_sampler_address_mode(0) == GFX_SAMPLER_REPEAT);
    assert(gfx_metal_sampler_address_mode(0x1u) == GFX_SAMPLER_MIRROR);
    assert(gfx_metal_sampler_address_mode(0x2u) == GFX_SAMPLER_CLAMP);
    assert(gfx_metal_sampler_address_mode(0x3u) == GFX_SAMPLER_CLAMP);

    assert(gfx_metal_pipeline_state_key(false, false, false, false) == 0);
    assert(gfx_metal_pipeline_state_key(true, false, false, false) == 1);
    assert(gfx_metal_pipeline_state_key(false, true, false, false) == 2);
    assert(gfx_metal_pipeline_state_key(false, false, true, false) == 4);
    assert(gfx_metal_pipeline_state_key(false, false, false, true) == 8);
    assert(gfx_metal_pipeline_state_key(true, true, true, true) == 15);
    puts("Metal state translation checks passed");
    return 0;
}
