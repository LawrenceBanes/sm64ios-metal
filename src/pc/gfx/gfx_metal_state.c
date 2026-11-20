#include "gfx_metal_state.h"

#include <math.h>

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
        GFX_METAL_PRESET_CLASSIC, 100u, 1u, 1u, false
    };
}

struct GfxMetalSettings gfx_metal_enhanced_settings(void) {
    return (struct GfxMetalSettings) {
        GFX_METAL_PRESET_ENHANCED, 100u, 4u, 8u, true
    };
}

struct GfxMetalSettings gfx_metal_cinematic_settings(void) {
    return (struct GfxMetalSettings) {
        GFX_METAL_PRESET_CINEMATIC, 100u, 4u, 16u, true
    };
}

struct GfxMetalSettings gfx_metal_performance_settings(void) {
    return (struct GfxMetalSettings) {
        GFX_METAL_PRESET_PERFORMANCE, 75u, 1u, 2u, true
    };
}

const char *gfx_metal_preset_name(uint32_t preset) {
    switch (preset) {
        case GFX_METAL_PRESET_CLASSIC: return "Classic";
        case GFX_METAL_PRESET_ENHANCED: return "Enhanced";
        case GFX_METAL_PRESET_CINEMATIC: return "Cinematic";
        case GFX_METAL_PRESET_PERFORMANCE: return "Performance";
        default: return "Classic";
    }
}

struct GfxMetalSettings gfx_metal_settings_sanitize(
    struct GfxMetalSettings requested, struct GfxMetalCapabilities capabilities) {
    struct GfxMetalSettings settings = requested;
    if (settings.preset > GFX_METAL_PRESET_PERFORMANCE)
        settings.preset = GFX_METAL_PRESET_CLASSIC;
    if (settings.preset == GFX_METAL_PRESET_CLASSIC)
        settings.hd_textures = false;

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

struct GfxMetalBloomSettings gfx_metal_bloom_settings(uint32_t profile) {
    static const struct GfxMetalBloomSettings profiles[] = {
        { 1.25f, 0.35f }, /* low */
        { 0.80f, 0.65f }, /* standard */
        { 0.55f, 1.00f }, /* high */
    };
    if (profile > 2u) profile = 1u;
    return profiles[profile];
}

struct GfxMetalColorAdjustments gfx_metal_color_adjustments_default(void) {
    return (struct GfxMetalColorAdjustments){ 0.0f, 1.0f, 1.0f, 0.0f };
}

struct GfxMetalColorAdjustments gfx_metal_color_adjustments_sanitize(
    struct GfxMetalColorAdjustments adjustments) {
    if (!isfinite(adjustments.brightness)) adjustments.brightness = 0.0f;
    if (!isfinite(adjustments.contrast)) adjustments.contrast = 1.0f;
    if (!isfinite(adjustments.saturation)) adjustments.saturation = 1.0f;
    if (!isfinite(adjustments.temperature)) adjustments.temperature = 0.0f;
    if (adjustments.brightness < -0.25f) adjustments.brightness = -0.25f;
    if (adjustments.brightness > 0.25f) adjustments.brightness = 0.25f;
    if (adjustments.contrast < 0.5f) adjustments.contrast = 0.5f;
    if (adjustments.contrast > 1.5f) adjustments.contrast = 1.5f;
    if (adjustments.saturation < 0.0f) adjustments.saturation = 0.0f;
    if (adjustments.saturation > 1.5f) adjustments.saturation = 1.5f;
    if (adjustments.temperature < -0.1f) adjustments.temperature = -0.1f;
    if (adjustments.temperature > 0.1f) adjustments.temperature = 0.1f;
    return adjustments;
}

struct GfxMetalColorAdjustments gfx_metal_color_adjustments_step(
    struct GfxMetalColorAdjustments adjustments, uint32_t parameter, int direction) {
    float step = direction < 0 ? -1.0f : 1.0f;
    switch (parameter) {
        case GFX_METAL_COLOR_BRIGHTNESS: adjustments.brightness += step * 0.05f; break;
        case GFX_METAL_COLOR_CONTRAST: adjustments.contrast += step * 0.10f; break;
        case GFX_METAL_COLOR_SATURATION: adjustments.saturation += step * 0.10f; break;
        case GFX_METAL_COLOR_TEMPERATURE: adjustments.temperature += step * 0.025f; break;
        default: return gfx_metal_color_adjustments_sanitize(adjustments);
    }
    return gfx_metal_color_adjustments_sanitize(adjustments);
}

struct GfxMetalAOAdjustments gfx_metal_ao_adjustments_default(void) {
    return (struct GfxMetalAOAdjustments){ 1.0f, 0.0f, 1.0f };
}

struct GfxMetalAOAdjustments gfx_metal_ao_adjustments_sanitize(
    struct GfxMetalAOAdjustments adjustments) {
    if (!isfinite(adjustments.radius_scale)) adjustments.radius_scale = 1.0f;
    if (!isfinite(adjustments.bias_offset)) adjustments.bias_offset = 0.0f;
    if (!isfinite(adjustments.intensity_scale)) adjustments.intensity_scale = 1.0f;
    if (adjustments.radius_scale < 0.5f) adjustments.radius_scale = 0.5f;
    if (adjustments.radius_scale > 2.0f) adjustments.radius_scale = 2.0f;
    if (adjustments.bias_offset < -0.02f) adjustments.bias_offset = -0.02f;
    if (adjustments.bias_offset > 0.04f) adjustments.bias_offset = 0.04f;
    if (adjustments.intensity_scale < 0.0f) adjustments.intensity_scale = 0.0f;
    if (adjustments.intensity_scale > 2.0f) adjustments.intensity_scale = 2.0f;
    return adjustments;
}

struct GfxMetalAOAdjustments gfx_metal_ao_adjustments_step(
    struct GfxMetalAOAdjustments adjustments, uint32_t parameter, int direction) {
    float step = direction < 0 ? -1.0f : 1.0f;
    switch (parameter) {
        case GFX_METAL_AO_RADIUS: adjustments.radius_scale += step * 0.25f; break;
        case GFX_METAL_AO_BIAS: adjustments.bias_offset += step * 0.005f; break;
        case GFX_METAL_AO_INTENSITY: adjustments.intensity_scale += step * 0.15f; break;
        default: return gfx_metal_ao_adjustments_sanitize(adjustments);
    }
    return gfx_metal_ao_adjustments_sanitize(adjustments);
}

struct GfxMetalFogAdjustments gfx_metal_fog_adjustments_default(void) {
    return (struct GfxMetalFogAdjustments){ 1.0f, 1.0f };
}

struct GfxMetalFogAdjustments gfx_metal_fog_adjustments_sanitize(
    struct GfxMetalFogAdjustments adjustments) {
    if (!isfinite(adjustments.density_scale)) adjustments.density_scale = 1.0f;
    if (!isfinite(adjustments.start_scale)) adjustments.start_scale = 1.0f;
    if (adjustments.density_scale < 0.25f) adjustments.density_scale = 0.25f;
    if (adjustments.density_scale > 2.0f) adjustments.density_scale = 2.0f;
    if (adjustments.start_scale < 0.5f) adjustments.start_scale = 0.5f;
    if (adjustments.start_scale > 2.0f) adjustments.start_scale = 2.0f;
    return adjustments;
}

struct GfxMetalFogAdjustments gfx_metal_fog_adjustments_step(
    struct GfxMetalFogAdjustments adjustments, uint32_t parameter, int direction) {
    float step = direction < 0 ? -1.0f : 1.0f;
    switch (parameter) {
        case GFX_METAL_FOG_DENSITY: adjustments.density_scale += step * 0.25f; break;
        case GFX_METAL_FOG_START: adjustments.start_scale += step * 0.25f; break;
        default: return gfx_metal_fog_adjustments_sanitize(adjustments);
    }
    return gfx_metal_fog_adjustments_sanitize(adjustments);
}

uint32_t gfx_metal_tone_mapping_sanitize(uint32_t mode) {
    return mode <= GFX_METAL_TONE_REINHARD ? mode : GFX_METAL_TONE_ACES;
}

const char *gfx_metal_tone_mapping_name_for_mode(uint32_t mode) {
    return gfx_metal_tone_mapping_sanitize(mode) == GFX_METAL_TONE_REINHARD ?
        "Reinhard" : "ACES";
}
