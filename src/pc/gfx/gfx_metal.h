#ifndef GFX_METAL_H
#define GFX_METAL_H

#include "gfx_rendering_api.h"

extern struct GfxRenderingAPI gfx_metal_api;

#ifdef __OBJC__
#import <UIKit/UIKit.h>
void gfx_metal_attach_to_view_controller(UIViewController *view_controller);
#endif

#endif
