#ifndef GFX_METAL_H
#define GFX_METAL_H

#include "gfx_rendering_api.h"

extern struct GfxRenderingAPI gfx_metal_api;

#ifdef __OBJC__
#import <UIKit/UIKit.h>
bool gfx_metal_attach_to_view_controller(UIViewController *view_controller);
bool gfx_metal_surface_is_ready(void);
void gfx_metal_set_scene_active(bool active);
#endif

#endif
