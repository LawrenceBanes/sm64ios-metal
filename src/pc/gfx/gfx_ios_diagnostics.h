#ifndef GFX_IOS_DIAGNOSTICS_H
#define GFX_IOS_DIAGNOSTICS_H

#include <stdbool.h>

#if defined(TARGET_OS_IOS)
void gfx_ios_diag_log(const char *format, ...);
bool gfx_ios_texture_cache_diagnostics_enabled(void);
#endif

#endif
