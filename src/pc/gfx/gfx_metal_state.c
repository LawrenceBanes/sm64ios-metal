#include "gfx_metal_state.h"

enum GfxSamplerAddressMode gfx_metal_sampler_address_mode(uint32_t n64_mode) {
    /* These are the G_TX_CLAMP and G_TX_MIRROR bits from PR/gbi.h. */
    if (n64_mode & 0x2u) return GFX_SAMPLER_CLAMP;
    if (n64_mode & 0x1u) return GFX_SAMPLER_MIRROR;
    return GFX_SAMPLER_REPEAT;
}

uint32_t gfx_metal_pipeline_state_key(bool depth_test, bool depth_write,
                                     bool blend, bool decal) {
    return (depth_test ? 1u : 0u) | (depth_write ? 2u : 0u) |
           (blend ? 4u : 0u) | (decal ? 8u : 0u);
}
