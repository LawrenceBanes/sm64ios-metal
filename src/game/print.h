#ifndef PRINT_H
#define PRINT_H

#include <stdbool.h>
#include <stddef.h>
#include <PR/ultratypes.h>

#define TEXRECT_MIN_X 10
#define TEXRECT_MAX_X 300
#define TEXRECT_MIN_Y 5
#define TEXRECT_MAX_Y 220

#define GLYPH_SPACE           -1
#define GLYPH_U               30
#define GLYPH_EXCLAMATION_PNT 36
#define GLYPH_TWO_EXCLAMATION 37
#define GLYPH_QUESTION_MARK   38
#define GLYPH_AMPERSAND       39
#define GLYPH_PERCENT         40
#define GLYPH_MULTIPLY        50
#define GLYPH_COIN            51
#define GLYPH_MARIO_HEAD      52
#define GLYPH_STAR            53
#define GLYPH_PERIOD          54
#define GLYPH_BETA_KEY        55
#define GLYPH_APOSTROPHE      56
#define GLYPH_DOUBLE_QUOTE    57
#define GLYPH_UMLAUT          58

void print_text_fmt_int(s32 x, s32 y, const char *str, s32 n);
void print_text(s32 x, s32 y, const char *str);
void print_text_centered(s32 x, s32 y, const char *str);
void render_text_labels(void);

/* CoopDX HUD adapter. These calls collect a bounded set of primitives during
 * HOOK_ON_HUD_RENDER; render_text_labels flushes them through the native RDP /
 * Metal display-list path later in the same frame. */
void mod_hud_reset_frame(void);
bool mod_hud_set_resolution(int resolution);
bool mod_hud_set_font(int font);
void mod_hud_set_color(u8 r, u8 g, u8 b, u8 a);
void mod_hud_reset_color(void);
int mod_hud_get_screen_width(void);
int mod_hud_get_screen_height(void);
bool mod_hud_measure_text(const char *text, float *width, float *height);
bool mod_hud_render_rect(float x, float y, float width, float height);
bool mod_hud_print_text(const char *text, float x, float y, float scale_x, float scale_y);

#endif // PRINT_H
