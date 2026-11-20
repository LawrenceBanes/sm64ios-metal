#include "pc/gfx/gfx_metal_state.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct GfxMetalBloomSettings bloom_low = gfx_metal_bloom_settings(0u);
    struct GfxMetalBloomSettings bloom_standard = gfx_metal_bloom_settings(1u);
    struct GfxMetalBloomSettings bloom_high = gfx_metal_bloom_settings(2u);
    struct GfxMetalBloomSettings bloom_invalid = gfx_metal_bloom_settings(99u);
    assert(bloom_low.threshold > bloom_standard.threshold && bloom_standard.threshold > bloom_high.threshold);
    assert(bloom_low.intensity < bloom_standard.intensity && bloom_standard.intensity < bloom_high.intensity);
    assert(bloom_invalid.threshold == bloom_standard.threshold && bloom_invalid.intensity == bloom_standard.intensity);
    struct GfxMetalColorAdjustments color = gfx_metal_color_adjustments_default();
    assert(color.brightness == 0.0f && color.contrast == 1.0f &&
           color.saturation == 1.0f && color.temperature == 0.0f);
    color = gfx_metal_color_adjustments_step(color, GFX_METAL_COLOR_BRIGHTNESS, 1);
    assert(color.brightness > 0.049f && color.brightness < 0.051f);
    color = gfx_metal_color_adjustments_step(color, GFX_METAL_COLOR_CONTRAST, -1);
    assert(color.contrast > 0.899f && color.contrast < 0.901f);
    color = gfx_metal_color_adjustments_step(color, GFX_METAL_COLOR_SATURATION, 1);
    assert(color.saturation > 1.099f && color.saturation < 1.101f);
    color = gfx_metal_color_adjustments_step(color, GFX_METAL_COLOR_TEMPERATURE, -1);
    assert(color.temperature < -0.024f && color.temperature > -0.026f);
    color = gfx_metal_color_adjustments_sanitize((struct GfxMetalColorAdjustments){
        9.0f, -1.0f, 8.0f, -9.0f });
    assert(color.brightness == 0.25f && color.contrast == 0.5f &&
           color.saturation == 1.5f && color.temperature == -0.1f);
    color = gfx_metal_color_adjustments_sanitize((struct GfxMetalColorAdjustments){
        NAN, INFINITY, -INFINITY, NAN });
    assert(color.brightness == 0.0f && color.contrast == 1.0f &&
           color.saturation == 1.0f && color.temperature == 0.0f);
    struct GfxMetalAOAdjustments ao = gfx_metal_ao_adjustments_default();
    assert(ao.radius_scale == 1.0f && ao.bias_offset == 0.0f && ao.intensity_scale == 1.0f);
    ao = gfx_metal_ao_adjustments_step(ao, GFX_METAL_AO_RADIUS, 1);
    ao = gfx_metal_ao_adjustments_step(ao, GFX_METAL_AO_BIAS, -1);
    ao = gfx_metal_ao_adjustments_step(ao, GFX_METAL_AO_INTENSITY, 1);
    assert(ao.radius_scale == 1.25f && ao.bias_offset < -0.004f && ao.intensity_scale > 1.14f);
    ao = gfx_metal_ao_adjustments_sanitize((struct GfxMetalAOAdjustments){
        99.0f, -99.0f, 99.0f });
    assert(ao.radius_scale == 2.0f && ao.bias_offset == -0.02f && ao.intensity_scale == 2.0f);
    ao = gfx_metal_ao_adjustments_sanitize((struct GfxMetalAOAdjustments){ NAN, INFINITY, NAN });
    assert(ao.radius_scale == 1.0f && ao.bias_offset == 0.0f && ao.intensity_scale == 1.0f);
    struct GfxMetalFogAdjustments fog = gfx_metal_fog_adjustments_default();
    assert(fog.density_scale == 1.0f && fog.start_scale == 1.0f);
    fog = gfx_metal_fog_adjustments_step(fog, GFX_METAL_FOG_DENSITY, -1);
    fog = gfx_metal_fog_adjustments_step(fog, GFX_METAL_FOG_START, 1);
    assert(fog.density_scale == 0.75f && fog.start_scale == 1.25f);
    fog = gfx_metal_fog_adjustments_sanitize((struct GfxMetalFogAdjustments){ -10.0f, 99.0f });
    assert(fog.density_scale == 0.25f && fog.start_scale == 2.0f);
    fog = gfx_metal_fog_adjustments_sanitize((struct GfxMetalFogAdjustments){ NAN, INFINITY });
    assert(fog.density_scale == 1.0f && fog.start_scale == 1.0f);
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

    struct GfxMetalSettings classic = gfx_metal_classic_settings();
    assert(classic.preset == GFX_METAL_PRESET_CLASSIC);
    assert(classic.render_scale_percent == 100 && classic.sample_count == 1 &&
           classic.anisotropy == 1 && !classic.hd_textures);
    assert(!gfx_metal_settings_use_offscreen(classic));

    struct GfxMetalCapabilities a16_like = { true, true, 16 };
    struct GfxMetalSettings enhanced = gfx_metal_settings_sanitize(
        gfx_metal_enhanced_settings(), a16_like);
    assert(enhanced.preset == GFX_METAL_PRESET_ENHANCED);
    assert(enhanced.render_scale_percent == 100 && enhanced.sample_count == 4 &&
           enhanced.anisotropy == 8 && enhanced.hd_textures);
    assert(gfx_metal_settings_use_offscreen(enhanced));
    struct GfxMetalSettings cinematic = gfx_metal_settings_sanitize(
        gfx_metal_cinematic_settings(), a16_like);
    assert(cinematic.preset == GFX_METAL_PRESET_CINEMATIC && cinematic.sample_count == 4 &&
           cinematic.anisotropy == 16 && cinematic.hd_textures);
    struct GfxMetalSettings performance = gfx_metal_settings_sanitize(
        gfx_metal_performance_settings(), a16_like);
    assert(performance.preset == GFX_METAL_PRESET_PERFORMANCE && performance.render_scale_percent == 75 &&
           performance.sample_count == 1 && performance.anisotropy == 2);
    assert(strcmp(gfx_metal_preset_name(GFX_METAL_PRESET_CINEMATIC), "Cinematic") == 0);
    assert(strcmp(gfx_metal_preset_name(GFX_METAL_PRESET_PERFORMANCE), "Performance") == 0);
    struct GfxMetalSettings unknown = { 99u, 100u, 1u, 1u, true };
    unknown = gfx_metal_settings_sanitize(unknown, a16_like);
    assert(unknown.preset == GFX_METAL_PRESET_CLASSIC && !unknown.hd_textures);

    struct GfxMetalCapabilities no_4x = { true, false, 4 };
    enhanced = gfx_metal_settings_sanitize(gfx_metal_enhanced_settings(), no_4x);
    assert(enhanced.sample_count == 2 && enhanced.anisotropy == 4);
    struct GfxMetalCapabilities classic_only = { false, false, 1 };
    enhanced = gfx_metal_settings_sanitize(gfx_metal_enhanced_settings(), classic_only);
    assert(enhanced.sample_count == 1 && enhanced.anisotropy == 1);
    assert(!gfx_metal_settings_use_offscreen(enhanced));

    struct GfxMetalSettings custom = { GFX_METAL_PRESET_ENHANCED, 75, 4, 16, true };
    custom = gfx_metal_settings_sanitize(custom, no_4x);
    assert(custom.render_scale_percent == 75 && custom.sample_count == 2 &&
           custom.anisotropy == 4 && custom.hd_textures);
    custom.preset = GFX_METAL_PRESET_CLASSIC;
    custom.hd_textures = true;
    custom = gfx_metal_settings_sanitize(custom, a16_like);
    assert(!custom.hd_textures);
    struct GfxMetalTargetSize target = { 0, 0 };
    assert(gfx_metal_target_size(2796, 1290, 75, &target));
    assert(target.width == 2097 && target.height == 968);
    assert(gfx_metal_target_size(1, 1, 50, &target));
    assert(target.width == 1 && target.height == 1);
    assert(!gfx_metal_target_size(2796, 1290, 60, &target));
    assert(!gfx_metal_target_size(0, 1290, 100, &target));

    assert(gfx_metal_pipeline_cache_index(1, 0) == 0);
    assert(gfx_metal_pipeline_cache_index(1, 15) == 15);
    assert(gfx_metal_pipeline_cache_index(2, 0) == 16);
    assert(gfx_metal_pipeline_cache_index(4, 15) == 47);
    assert(gfx_metal_pipeline_cache_index(8, 0) == UINT32_MAX);
    assert(gfx_metal_pipeline_cache_index(4, 16) == UINT32_MAX);
    puts("Metal state translation checks passed");
    return 0;
}
