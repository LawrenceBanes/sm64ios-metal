#ifndef GFX_METAL_H
#define GFX_METAL_H

#include "gfx_rendering_api.h"
#include "gfx_metal_state.h"

extern struct GfxRenderingAPI gfx_metal_api;

#if defined(TARGET_OS_IOS)
bool gfx_metal_hd_textures_enabled(void);
bool gfx_metal_ssao_enabled(void);
void gfx_metal_set_ssao_enabled(bool enabled);
uint32_t gfx_metal_ssao_debug_mode(void);
void gfx_metal_cycle_ssao_mode(void);
bool gfx_metal_effects_enabled(void);
void gfx_metal_set_effects_enabled(bool enabled);
bool gfx_metal_offscreen_fallback_active(void);
bool gfx_metal_shadows_enabled(void);
void gfx_metal_set_shadows_enabled(bool enabled);
uint32_t gfx_metal_shadow_resolution(void);
void gfx_metal_set_shadow_resolution(uint32_t resolution);
void gfx_metal_cycle_shadow_resolution(void);
uint32_t gfx_metal_sun_preset(void);
void gfx_metal_set_sun_preset(uint32_t preset);
void gfx_metal_cycle_sun_preset(void);
void gfx_metal_get_sun_direction(float direction[3]);
const char *gfx_metal_sun_preset_name(void);
uint32_t gfx_metal_sun_intensity(void);
void gfx_metal_set_sun_intensity(uint32_t intensity);
void gfx_metal_cycle_sun_intensity(void);
bool gfx_metal_god_rays_enabled(void);
void gfx_metal_set_god_rays_enabled(bool enabled);
uint32_t gfx_metal_god_ray_quality(void);
void gfx_metal_set_god_ray_quality(uint32_t quality);
void gfx_metal_cycle_god_ray_quality(void);
uint32_t gfx_metal_ssao_quality(void);
void gfx_metal_set_ssao_quality(uint32_t quality);
void gfx_metal_cycle_ssao_quality(void);
struct GfxMetalAOAdjustments gfx_metal_get_ao_adjustments(void);
void gfx_metal_adjust_ao(uint32_t parameter, int direction);
void gfx_metal_reset_ao_adjustments(void);
bool gfx_metal_post_enabled(void);
void gfx_metal_set_post_enabled(bool enabled);
bool gfx_metal_fxaa_enabled(void);
void gfx_metal_set_fxaa_enabled(bool enabled);
float gfx_metal_exposure_stops(void);
void gfx_metal_step_exposure(void);
uint32_t gfx_metal_color_grade(void);
void gfx_metal_cycle_color_grade(void);
void gfx_metal_set_color_grade(uint32_t grade);
struct GfxMetalColorAdjustments gfx_metal_get_color_adjustments(void);
void gfx_metal_adjust_color(uint32_t parameter, int direction);
void gfx_metal_reset_color_adjustments(void);
bool gfx_metal_fog_enabled(void);
void gfx_metal_set_fog_enabled(bool enabled);
uint32_t gfx_metal_fog_profile(void);
void gfx_metal_cycle_fog_profile(void);
void gfx_metal_set_fog_profile(uint32_t profile);
struct GfxMetalFogAdjustments gfx_metal_get_fog_adjustments(void);
void gfx_metal_adjust_fog(uint32_t parameter, int direction);
void gfx_metal_reset_fog_adjustments(void);
uint32_t gfx_metal_sky_preset(void);
void gfx_metal_cycle_sky_preset(void);
void gfx_metal_set_sky_preset(uint32_t preset);
bool gfx_metal_bloom_enabled(void);
void gfx_metal_set_bloom_enabled(bool enabled);
uint32_t gfx_metal_bloom_profile(void);
void gfx_metal_set_bloom_profile(uint32_t profile);
#endif

#ifdef __OBJC__
#import <UIKit/UIKit.h>
bool gfx_metal_attach_to_view_controller(UIViewController *view_controller);
bool gfx_metal_surface_is_ready(void);
void gfx_metal_set_scene_active(bool active);
struct GfxMetalSettings gfx_metal_get_settings(void);
struct GfxMetalCapabilities gfx_metal_get_capabilities(void);
void gfx_metal_apply_settings(struct GfxMetalSettings settings);
void gfx_metal_get_performance_stats(struct GfxMetalPerformanceStats *stats);
void gfx_metal_set_cpu_frame_time_ms(double milliseconds);
#endif

#endif
