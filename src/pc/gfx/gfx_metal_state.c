#include "gfx_metal_state.h"

enum GfxSamplerAddressMode gfx_metal_sampler_address_mode(uint32_t n64_mode) {
    /* These are the G_TX_CLAMP and G_TX_MIRROR bits from PR/gbi.h. */
    if (n64_mode & 0x2u) return GFX_SAMPLER_CLAMP;
    if (n64_mode & 0x1u) return GFX_SAMPLER_MIRROR;
    return GFX_SAMPLER_REPEAT;
}

bool gfx_metal_sampler_state_matches(bool current_valid,
                                     struct GfxMetalSamplerState current,
                                     struct GfxMetalSamplerState desired) {
    if (!current_valid || current.linear != desired.linear) return false;
    return gfx_metal_sampler_address_mode(current.cms) == gfx_metal_sampler_address_mode(desired.cms) &&
           gfx_metal_sampler_address_mode(current.cmt) == gfx_metal_sampler_address_mode(desired.cmt);
}

uint32_t gfx_metal_pipeline_state_key(bool depth_test, bool depth_write,
                                     bool blend, bool decal) {
    return (depth_test ? 1u : 0u) | (depth_write ? 2u : 0u) |
           (blend ? 4u : 0u) | (decal ? 8u : 0u);
}

struct GfxMetalSettings gfx_metal_classic_settings(void) {
    return (struct GfxMetalSettings) {
        GFX_METAL_PRESET_CLASSIC, 100u, 1u, 1u
    };
}

struct GfxMetalSettings gfx_metal_enhanced_settings(void) {
    return (struct GfxMetalSettings) {
        GFX_METAL_PRESET_ENHANCED, 100u, 4u, 8u
    };
}

struct GfxMetalSettings gfx_metal_settings_sanitize(
    struct GfxMetalSettings requested, struct GfxMetalCapabilities capabilities) {
    struct GfxMetalSettings settings = requested;
    if (settings.preset > GFX_METAL_PRESET_ENHANCED)
        settings.preset = GFX_METAL_PRESET_CLASSIC;

    if (settings.render_scale_percent != 50u &&
        settings.render_scale_percent != 75u &&
        settings.render_scale_percent != 100u) {
        settings.render_scale_percent = 100u;
    }

    if (settings.sample_count >= 4u && capabilities.sample_count_4) {
        settings.sample_count = 4u;
    } else if (settings.sample_count >= 2u && capabilities.sample_count_2) {
        settings.sample_count = 2u;
    } else {
        settings.sample_count = 1u;
    }

    uint32_t max_anisotropy = capabilities.max_anisotropy;
    if (max_anisotropy > 16u) max_anisotropy = 16u;
    if (max_anisotropy == 0u) max_anisotropy = 1u;
    uint32_t requested_anisotropy = requested.anisotropy;
    if (requested_anisotropy > max_anisotropy)
        requested_anisotropy = max_anisotropy;
    static const uint32_t anisotropy_levels[] = { 1u, 2u, 4u, 8u, 16u };
    settings.anisotropy = 1u;
    for (size_t i = 0; i < sizeof(anisotropy_levels) / sizeof(anisotropy_levels[0]); ++i) {
        if (anisotropy_levels[i] <= requested_anisotropy)
            settings.anisotropy = anisotropy_levels[i];
    }
    return settings;
}

bool gfx_metal_settings_use_offscreen(struct GfxMetalSettings settings) {
    return settings.render_scale_percent != 100u || settings.sample_count > 1u;
}

bool gfx_metal_target_size(uint32_t drawable_width, uint32_t drawable_height,
                           uint32_t render_scale_percent,
                           struct GfxMetalTargetSize *target_size) {
    if (!target_size || drawable_width == 0u || drawable_height == 0u ||
        (render_scale_percent != 50u && render_scale_percent != 75u &&
         render_scale_percent != 100u)) {
        return false;
    }
    uint64_t width = (uint64_t)drawable_width * render_scale_percent + 50u;
    uint64_t height = (uint64_t)drawable_height * render_scale_percent + 50u;
    target_size->width = (uint32_t)(width / 100u);
    target_size->height = (uint32_t)(height / 100u);
    if (target_size->width == 0u) target_size->width = 1u;
    if (target_size->height == 0u) target_size->height = 1u;
    return true;
}

uint32_t gfx_metal_pipeline_cache_index(uint32_t sample_count,
                                        uint32_t render_state_key) {
    uint32_t sample_index;
    switch (sample_count) {
        case 1u: sample_index = 0u; break;
        case 2u: sample_index = 1u; break;
        case 4u: sample_index = 2u; break;
        default: return UINT32_MAX;
    }
    if (render_state_key >= 16u) return UINT32_MAX;
    return sample_index * 16u + render_state_key;
}
