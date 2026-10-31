#import <Foundation/Foundation.h>

/* The standalone Apple SDK validation target links gfx_metal.m without the
 * full UIKit host. Keep these app-owned diagnostic callbacks linkable while
 * leaving the real implementation in gfx_uikit.m for the application. */
void gfx_ios_diag_log(const char *format, ...) {
    (void)format;
}

void gfx_ios_diag_mark_first_game_frame(void) {
}
