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
uint32_t gfx_metal_ssao_quality(void);
void gfx_metal_set_ssao_quality(uint32_t quality);
void gfx_metal_cycle_ssao_quality(void);
bool gfx_metal_post_enabled(void);
void gfx_metal_set_post_enabled(bool enabled);
bool gfx_metal_fxaa_enabled(void);
void gfx_metal_set_fxaa_enabled(bool enabled);
float gfx_metal_exposure_stops(void);
void gfx_metal_step_exposure(void);
uint32_t gfx_metal_color_grade(void);
void gfx_metal_cycle_color_grade(void);
void gfx_metal_set_color_grade(uint32_t grade);
bool gfx_metal_fog_enabled(void);
void gfx_metal_set_fog_enabled(bool enabled);
uint32_t gfx_metal_fog_profile(void);
void gfx_metal_cycle_fog_profile(void);
void gfx_metal_set_fog_profile(uint32_t profile);
uint32_t gfx_metal_sky_preset(void);
void gfx_metal_cycle_sky_preset(void);
void gfx_metal_set_sky_preset(uint32_t preset);
bool gfx_metal_bloom_enabled(void);
void gfx_metal_set_bloom_enabled(bool enabled);
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
