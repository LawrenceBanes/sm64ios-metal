#ifndef GFX_METAL_STATE_H
#define GFX_METAL_STATE_H

#include <stdbool.h>
#include <stdint.h>

enum GfxSamplerAddressMode {
    GFX_SAMPLER_REPEAT = 0,
    GFX_SAMPLER_MIRROR = 1,
    GFX_SAMPLER_CLAMP = 2,
};

enum GfxSamplerAddressMode gfx_metal_sampler_address_mode(uint32_t n64_mode);
uint32_t gfx_metal_pipeline_state_key(bool depth_test, bool depth_write,
                                     bool blend, bool decal);

#endif
