#include "pc/gfx/gfx_metal_state.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(gfx_metal_sampler_address_mode(0) == GFX_SAMPLER_REPEAT);
    assert(gfx_metal_sampler_address_mode(0x1u) == GFX_SAMPLER_MIRROR);
    assert(gfx_metal_sampler_address_mode(0x2u) == GFX_SAMPLER_CLAMP);
    assert(gfx_metal_sampler_address_mode(0x3u) == GFX_SAMPLER_CLAMP);

    struct GfxMetalSamplerState nearest_clamp = { false, 0x2u, 0x2u };
    struct GfxMetalSamplerState linear_clamp = { true, 0x2u, 0x2u };
    struct GfxMetalSamplerState linear_repeat = { true, 0u, 0u };
    struct GfxMetalSamplerState linear_clamp_with_other_bits = { true, 0x6u, 0x2u };
    /* Reproduce the device-log transition: texture A requests linear clamp,
     * texture B leaves the shared Metal slot at nearest clamp, then A is
     * selected again. The backend must detect and restore A's sampler. */
    struct GfxMetalSamplerState active_slot = nearest_clamp;
    struct GfxMetalSamplerState texture_a = linear_clamp;
    assert(!gfx_metal_sampler_state_matches(false, active_slot, texture_a));
    assert(!gfx_metal_sampler_state_matches(true, active_slot, texture_a));
    active_slot = texture_a;
    assert(gfx_metal_sampler_state_matches(true, active_slot, texture_a));
    assert(!gfx_metal_sampler_state_matches(true, linear_clamp, linear_repeat));
    assert(gfx_metal_sampler_state_matches(true, linear_clamp, linear_clamp_with_other_bits));
    assert(!gfx_metal_sampler_state_matches(true, linear_clamp,
                                            (struct GfxMetalSamplerState){ true, 0x2u, 0u }));
    assert(!gfx_metal_sampler_state_matches(true, linear_clamp,
                                            (struct GfxMetalSamplerState){ true, 0u, 0x2u }));

    assert(gfx_metal_pipeline_state_key(false, false, false, false) == 0);
    assert(gfx_metal_pipeline_state_key(true, false, false, false) == 1);
    assert(gfx_metal_pipeline_state_key(false, true, false, false) == 2);
    assert(gfx_metal_pipeline_state_key(false, false, true, false) == 4);
    assert(gfx_metal_pipeline_state_key(false, false, false, true) == 8);
    assert(gfx_metal_pipeline_state_key(true, true, true, true) == 15);
    puts("Metal state translation checks passed");
    return 0;
}
