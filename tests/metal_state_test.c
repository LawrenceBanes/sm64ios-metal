#include "pc/gfx/gfx_metal_state.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
