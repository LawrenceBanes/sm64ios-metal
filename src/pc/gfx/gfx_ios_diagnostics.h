#ifndef GFX_IOS_DIAGNOSTICS_H
#define GFX_IOS_DIAGNOSTICS_H

#include <stdbool.h>

enum GfxIOSRenderDiagnosticMode {
    GFX_IOS_RENDER_NORMAL = 0,
    GFX_IOS_RENDER_UNTEXTURED = 1,
    GFX_IOS_RENDER_UV = 2,
    GFX_IOS_RENDER_TEXTURE_ID = 3,
    GFX_IOS_RENDER_TRIANGLES = 4,
};

#if defined(TARGET_OS_IOS)
void gfx_ios_diag_log(const char *format, ...);
bool gfx_ios_texture_cache_diagnostics_enabled(void);
int gfx_ios_render_diagnostic_mode(void);
#endif

#if defined(IOS_BUILD)
void gfx_ios_diag_log(const char *format, ...);
/* Game-thread portal feedback is marshalled to the native UIKit diagnostic
 * overlay; this does not mutate UIKit from the simulation thread. */
void gfx_ios_portal_status(const char *message);
#endif

#endif
