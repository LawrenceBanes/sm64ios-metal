#ifndef GFX_METAL_H
#define GFX_METAL_H

#include "gfx_rendering_api.h"
#include "gfx_metal_state.h"

extern struct GfxRenderingAPI gfx_metal_api;

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
