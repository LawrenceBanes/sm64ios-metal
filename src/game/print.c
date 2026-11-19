#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "game_init.h"
#include "memory.h"
#include "ingame_menu.h"
#include "print.h"
#include "segment2.h"
#include "gfx_dimensions.h"

/**
 * This file handles printing and formatting the colorful text that
 * appears when printing things such as "PRESS START".
 */

struct TextLabel {
    u32 x;
    u32 y;
    s16 length;
    char buffer[50];
};

/**
 * Stores the text to be rendered on screen
 * and how they are to be rendered.
 */
struct TextLabel *sTextLabels[256];
s16 sTextLabelsCount = 0;

#define MOD_HUD_COMMAND_CAPACITY 256
#define MOD_HUD_TEXT_CAPACITY 256
#define MOD_HUD_GLYPH_CAPACITY 512

enum ModHudCommandType { MOD_HUD_COMMAND_RECT, MOD_HUD_COMMAND_TEXT };
struct ModHudCommand {
    enum ModHudCommandType type;
    f32 x, y, width, height, scale_x, scale_y;
    u8 r, g, b, a;
    char text[MOD_HUD_TEXT_CAPACITY];
};

static struct ModHudCommand sModHudCommands[MOD_HUD_COMMAND_CAPACITY];
static unsigned sModHudCommandCount;
static unsigned sModHudGlyphCount;
static int sModHudResolution;
static int sModHudFont;
static u8 sModHudColor[4] = { 255, 255, 255, 255 };

/* render_generic_char is the engine's normal IA font glyph loader. */
extern void render_generic_char(u8 c);
#if !defined(VERSION_JP) && !defined(VERSION_SH)
extern u8 gDialogCharWidths[256];
#endif

static f32 mod_hud_resolution_scale(void) {
    /* The local renderer's virtual screen is 240 pixels high. CoopDX's DJUI
     * space is 720 high at the reference scale; N64 space remains 240 high. */
    return sModHudResolution == 0 ? (1.0f / 3.0f) : 1.0f;
}

static int mod_hud_font_code(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        return ASCII_TO_DIALOG(c);
    switch (c) {
        case ' ': return -2;
        case '\n': return -3;
        case '\'': return 0x3E;
        case '.': return 0x3F;
        case ',': return 0x6F;
        case '/': return 0x9F;
#ifdef VERSION_US
        case '(': return 0xE1;
        case ')': return 0xE3;
        case '&': return 0xE5;
        case '!': return 0xF2;
        case '%': return 0xF3;
        case '?': return 0xF4;
        case '"': return 0xF5;
        case '~': return 0xF7;
#endif
        default: return -1;
    }
}

static u8 mod_hud_glyph_width(int code, unsigned char original) {
#if !defined(VERSION_JP) && !defined(VERSION_SH)
    if (code >= 0 && gDialogCharWidths[code] != 0) return gDialogCharWidths[code];
#else
    (void)code;
#endif
    return original == ' ' ? 4 : 8;
}

void mod_hud_reset_frame(void) {
    sModHudCommandCount = 0;
    sModHudGlyphCount = 0;
    sModHudResolution = 0;
    sModHudFont = 0;
    sModHudColor[0] = sModHudColor[1] = sModHudColor[2] = sModHudColor[3] = 255;
}

bool mod_hud_set_resolution(int resolution) {
    if (resolution < 0 || resolution > 1) return false;
    sModHudResolution = resolution;
    return true;
}

bool mod_hud_set_font(int font) {
    /* The native SM64 IA font is the only font currently decoded by this
     * renderer adapter. FONT_NORMAL uses that real engine font. */
    if (font != 0) return false;
    sModHudFont = font;
    return true;
}

void mod_hud_set_color(u8 r, u8 g, u8 b, u8 a) {
    sModHudColor[0] = r;
    sModHudColor[1] = g;
    sModHudColor[2] = b;
    sModHudColor[3] = a;
}

void mod_hud_reset_color(void) {
    mod_hud_set_color(255, 255, 255, 255);
}

int mod_hud_get_screen_width(void) {
    f32 width = GFX_DIMENSIONS_ASPECT_RATIO * SCREEN_HEIGHT;
    if (sModHudResolution == 0) width *= 3.0f;
    return (int)(width + 0.5f);
}

int mod_hud_get_screen_height(void) {
    return sModHudResolution == 0 ? SCREEN_HEIGHT * 3 : SCREEN_HEIGHT;
}

bool mod_hud_measure_text(const char *text, float *width, float *height) {
    if (!text || !width || !height || sModHudFont != 0) return false;
    size_t length = strlen(text);
    if (length > MOD_HUD_TEXT_CAPACITY - 1) return false;
    f32 line_width = 0.0f, max_width = 0.0f;
    unsigned lines = 1;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        int code = mod_hud_font_code(c);
        if (code == -3) {
            if (line_width > max_width) max_width = line_width;
            line_width = 0.0f;
            ++lines;
        } else {
            line_width += mod_hud_glyph_width(code, c);
        }
    }
    if (line_width > max_width) max_width = line_width;
    *width = max_width;
    *height = 16.0f * lines;
    return true;
}

static bool mod_hud_queue_text(const char *text, f32 x, f32 y, f32 scale_x, f32 scale_y) {
    if (!text || !isfinite(x) || !isfinite(y) || !isfinite(scale_x) || !isfinite(scale_y) ||
        scale_x <= 0.0f || scale_y <= 0.0f || scale_x > 64.0f || scale_y > 64.0f ||
        sModHudFont != 0 || sModHudCommandCount >= MOD_HUD_COMMAND_CAPACITY) return false;
    size_t length = strlen(text);
    if (length >= MOD_HUD_TEXT_CAPACITY) return false;
    if (length > MOD_HUD_GLYPH_CAPACITY - sModHudGlyphCount) return false;
    f32 resolution_scale = mod_hud_resolution_scale();
    struct ModHudCommand *command = &sModHudCommands[sModHudCommandCount++];
    memset(command, 0, sizeof(*command));
    command->type = MOD_HUD_COMMAND_TEXT;
    command->x = GFX_DIMENSIONS_FROM_LEFT_EDGE(0) + x * resolution_scale;
    command->y = y * resolution_scale;
    command->scale_x = scale_x * resolution_scale;
    command->scale_y = scale_y * resolution_scale;
    memcpy(command->text, text, length + 1);
    memcpy(&command->r, sModHudColor, sizeof(sModHudColor));
    sModHudGlyphCount += (unsigned)length;
    return true;
}

bool mod_hud_render_rect(float x, float y, float width, float height) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height) ||
        width <= 0.0f || height <= 0.0f || width > 8192.0f || height > 8192.0f ||
        sModHudCommandCount >= MOD_HUD_COMMAND_CAPACITY) return false;
    f32 resolution_scale = mod_hud_resolution_scale();
    struct ModHudCommand *command = &sModHudCommands[sModHudCommandCount++];
    memset(command, 0, sizeof(*command));
    command->type = MOD_HUD_COMMAND_RECT;
    command->x = GFX_DIMENSIONS_FROM_LEFT_EDGE(0) + x * resolution_scale;
    command->y = y * resolution_scale;
    command->width = width * resolution_scale;
    command->height = height * resolution_scale;
    memcpy(&command->r, sModHudColor, sizeof(sModHudColor));
    return true;
}

bool mod_hud_print_text(const char *text, float x, float y, float scale_x, float scale_y) {
    return mod_hud_queue_text(text, x, y, scale_x, scale_y);
}

/**
 * Returns n to the exponent power, only for non-negative powers.
 */
s32 int_pow(s32 n, s32 exponent) {
    s32 result = 1;
    s32 i;

    for (i = 0; i < exponent; i++) {
        result = n * result;
    }

    return result;
}

/**
 * Formats an integer n for print by fitting it to width, prefixing with a negative,
 * and converting the base.
 */
void format_integer(s32 n, s32 base, char *dest, s32 *totalLength, u8 width, s8 zeroPad) {
    u32 powBase;
    s32 numDigits = 0;
    s32 i;
    s32 len = 0;
    s8 digit;
    s8 negative = FALSE;
    char pad;

    if (zeroPad == TRUE) {
        pad = '0';
    } else {
        pad = -1;
    }

    if (n != 0) {
        // Formats a negative number for negative prefix.
        if (n < 0) {
            n = -n;
            negative = TRUE;
        }

        // Increments the number of digits until length is long enough.
        while (1) {
            powBase = int_pow(base, numDigits);

            if (powBase > (u32) n) {
                break;
            }

            numDigits++;
        }

        // Add leading pad to fit width.
        if (width > numDigits) {
            for (len = 0; len < width - numDigits; len++) dest[len] = pad;

            // Needs 1 length to print negative prefix.
            if (negative == TRUE) {
                len--;
            }
        }

        // Use 'M' prefix to indicate negative numbers.
        if (negative == TRUE) {
            dest[len] = 'M';
            len++;
        }

        // Transfer the digits into the proper base.
        for (i = numDigits - 1; i >= 0; i--) {
            powBase = int_pow(base, i);
            digit = n / powBase;

            // FIXME: Why doesn't [] match?
            if (digit < 10) {
                *(dest + len + numDigits - 1 - i) = digit + '0';
            } else {
                *(dest + len + numDigits - 1 - i) = digit + '7';
            }

            n -= digit * powBase;
        }
    } else // n is zero.
    {
        numDigits = 1;
        if (width > numDigits) {
            for (len = 0; len < width - numDigits; len++) dest[len] = pad;
        }
        dest[len] = '0';
    }

    *totalLength += numDigits + len;
}

/**
 * Determines the width of the number for printing, writing to 'width'.
 * Additionally, this determines if a number should be zero-padded,
 * writing to 'zeroPad'.
 */
void parse_width_field(const char *str, s32 *srcIndex, u8 *width, s8 *zeroPad) {
    s8 digits[12]; // unknown length
    s8 digitsLen = 0;
    s16 i;

    // If first character is 0, then the string should be zero padded.
    if (str[*srcIndex] == '0') {
        *zeroPad = TRUE;
    }

    // Read width digits up until the 'd' or 'x' format specifier.
    while (str[*srcIndex] != 'd' && str[*srcIndex] != 'x') {
        digits[digitsLen] = str[*srcIndex] - '0';

        if (digits[digitsLen] < 0 || digits[digitsLen] >= 10) // not a valid digit
        {
            *width = 0;
            return;
        }

        digitsLen++;
        (*srcIndex)++;
    }

    // No digits
    if (digitsLen == 0) {
        return;
    }

    // Sum the digits to calculate the total width.
    for (i = 0; i < digitsLen - 1; i++) {
        *width = *width + digits[i] * ((digitsLen - i - 1) * 10);
    }

    *width = *width + digits[digitsLen - 1];
}

/**
 * Takes a number, finds the intended base, formats the number, and prints it
 * at the given X & Y coordinates.
 *
 * Warning: this fails on too large numbers, because format_integer has bugs
 * related to overflow. For romhacks, prefer sprintf + print_text.
 */
void print_text_fmt_int(s32 x, s32 y, const char *str, s32 n) {
    char c = 0;
    s8 zeroPad = FALSE;
    u8 width = 0;
    s32 base = 0;
    s32 len = 0;
    s32 srcIndex = 0;

    // Don't continue if there is no memory to do so.
    if ((sTextLabels[sTextLabelsCount] = mem_pool_alloc(gEffectsMemoryPool,
                                                        sizeof(struct TextLabel))) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x;
    sTextLabels[sTextLabelsCount]->y = y;

    c = str[srcIndex];

    while (c != 0) {
        if (c == '%') {
            srcIndex++;

            parse_width_field(str, &srcIndex, &width, &zeroPad);

            if (str[srcIndex] != 'd' && str[srcIndex] != 'x') {
                break;
            }
            if (str[srcIndex] == 'd') {
                base = 10;
            }
            if (str[srcIndex] == 'x') {
                base = 16;
            }

            srcIndex++;

            format_integer(n, base, sTextLabels[sTextLabelsCount]->buffer + len, &len, width, zeroPad);
        } else // straight copy
        {
            sTextLabels[sTextLabelsCount]->buffer[len] = c;
            len++;
            srcIndex++;
        }
        c = str[srcIndex];
    }

    sTextLabels[sTextLabelsCount]->length = len;
    sTextLabelsCount++;
}

/**
 * Prints text in the colorful lettering at given X, Y coordinates.
 */
void print_text(s32 x, s32 y, const char *str) {
    char c = 0;
    s32 length = 0;
    s32 srcIndex = 0;

    // Don't continue if there is no memory to do so.
    if ((sTextLabels[sTextLabelsCount] = mem_pool_alloc(gEffectsMemoryPool,
                                                        sizeof(struct TextLabel))) == NULL) {
        return;
    }

    sTextLabels[sTextLabelsCount]->x = x;
    sTextLabels[sTextLabelsCount]->y = y;

    c = str[srcIndex];

    // Set the array with the text to print while finding length.
    while (c != 0) {
        sTextLabels[sTextLabelsCount]->buffer[length] = c;
        length++;
        srcIndex++;
        c = str[srcIndex];
    }

    sTextLabels[sTextLabelsCount]->length = length;
    sTextLabelsCount++;
}

/**
 * Prints text in the colorful lettering centered at given X, Y coordinates.
 */
void print_text_centered(s32 x, s32 y, const char *str) {
    char c = 0;
    UNUSED s8 unused1 = 0;
    UNUSED s32 unused2 = 0;
    s32 length = 0;
    s32 srcIndex = 0;

    // Don't continue if there is no memory to do so.
    if ((sTextLabels[sTextLabelsCount] = mem_pool_alloc(gEffectsMemoryPool,
                                                        sizeof(struct TextLabel))) == NULL) {
        return;
    }

    c = str[srcIndex];

    // Set the array with the text to print while finding length.
    while (c != 0) {
        sTextLabels[sTextLabelsCount]->buffer[length] = c;
        length++;
        srcIndex++;
        c = str[srcIndex];
    }

    sTextLabels[sTextLabelsCount]->length = length;
    sTextLabels[sTextLabelsCount]->x = x - length * 12 / 2;
    sTextLabels[sTextLabelsCount]->y = y;
    sTextLabelsCount++;
}

/**
 * Converts a char into the proper colorful glyph for the char.
 */
s8 char_to_glyph_index(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 55;
    }

    if (c >= 'a' && c <= 'z') {
        return c - 87;
    }

    if (c >= '0' && c <= '9') {
        return c - 48;
    }

    if (c == ' ') {
        return GLYPH_SPACE;
    }

    if (c == '!') {
        return GLYPH_EXCLAMATION_PNT; // !, JP only
    }

    if (c == '#') {
        return GLYPH_TWO_EXCLAMATION; // !!, JP only
    }

    if (c == '?') {
        return GLYPH_QUESTION_MARK; // ?, JP only
    }

    if (c == '&') {
        return GLYPH_AMPERSAND; // &, JP only
    }

    if (c == '%') {
        return GLYPH_PERCENT; // %, JP only
    }

    if (c == '*') {
        return GLYPH_MULTIPLY; // x
    }

    if (c == '+') {
        return GLYPH_COIN; // coin
    }

    if (c == ',') {
        return GLYPH_MARIO_HEAD; // Imagine I drew Mario's head
    }

    if (c == '-') {
        return GLYPH_STAR; // star
    }

    if (c == '.') {
        return GLYPH_PERIOD; // large shaded dot, JP only
    }

    if (c == '/') {
        return GLYPH_BETA_KEY; // beta key, JP only. Reused for Ü in EU.
    }

    return GLYPH_SPACE;
}

/**
 * Adds an individual glyph to be rendered.
 */
void add_glyph_texture(s8 glyphIndex) {
    const u8 *const *glyphs = segmented_to_virtual(main_hud_lut);

    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, glyphs[glyphIndex]);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_load_tex_block);
}

/**
 * Renders the glyph that's set at the given position.
 */
void render_textrect(s32 x, s32 y, s32 pos) {
    s32 rectBaseX = x + pos * 12;
    s32 rectBaseY = 224 - y;
    s32 rectX;
    s32 rectY;

    rectX = rectBaseX;
    rectY = rectBaseY;
    gSPTextureRectangle(gDisplayListHead++, rectX << 2, rectY << 2, (rectX + 15) << 2,
                        (rectY + 15) << 2, G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);
}

static s16 mod_hud_s16(f32 value) {
    if (value < -32768.0f) return -32768;
    if (value > 32767.0f) return 32767;
    return (s16)lrintf(value);
}

static void mod_hud_render_rect_command(const struct ModHudCommand *command) {
    Vtx *vertices = alloc_display_list(4 * sizeof(*vertices));
    if (!vertices) return;

    s16 left = mod_hud_s16(command->x);
    s16 right = mod_hud_s16(command->x + command->width);
    s16 bottom = mod_hud_s16(SCREEN_HEIGHT - command->y - command->height);
    s16 top = mod_hud_s16(SCREEN_HEIGHT - command->y);
    if (right <= left || top <= bottom) return;

    const s16 x[4] = { left, right, right, left };
    const s16 y[4] = { bottom, bottom, top, top };
    for (int i = 0; i < 4; ++i) {
        vertices[i].v.ob[0] = x[i];
        vertices[i].v.ob[1] = y[i];
        vertices[i].v.ob[2] = 0;
        vertices[i].v.flag = 0;
        vertices[i].v.tc[0] = vertices[i].v.tc[1] = 0;
        vertices[i].v.cn[0] = command->r;
        vertices[i].v.cn[1] = command->g;
        vertices[i].v.cn[2] = command->b;
        vertices[i].v.cn[3] = command->a;
    }

    gDPPipeSync(gDisplayListHead++);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING | G_CULL_BACK | G_CULL_FRONT);
    gSPVertex(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(vertices), 4, 0);
    gSP2Triangles(gDisplayListHead++, 0, 1, 2, 0x0, 0, 2, 3, 0x0);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

static void mod_hud_render_text_line(const struct ModHudCommand *command, size_t start, size_t end,
                                     unsigned line_index) {
    if (command->scale_x <= 0.0f || command->scale_y <= 0.0f) return;
    Mtx *scale = alloc_display_list(sizeof(*scale));
    Mtx *translate = alloc_display_list(sizeof(*translate));
    if (!scale || !translate) return;

    f32 y = command->y + (16.0f * command->scale_y * line_index);
    guScale(scale, command->scale_x, command->scale_y, 1.0f);
    guTranslate(translate, command->x / command->scale_x,
                (SCREEN_HEIGHT - y - 16.0f * command->scale_y) / command->scale_y, 0.0f);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(scale), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(translate), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);

    for (size_t i = start; i < end; ++i) {
        unsigned char c = (unsigned char)command->text[i];
        int code = mod_hud_font_code(c);
        u8 advance = mod_hud_glyph_width(code, c);
        if (code >= 0) {
            render_generic_char((u8)code);
            if (advance) create_dl_translation_matrix(MENU_MTX_NOPUSH, (f32)advance, 0.0f, 0.0f);
        } else if (code == -2) {
            if (advance) create_dl_translation_matrix(MENU_MTX_NOPUSH, (f32)advance, 0.0f, 0.0f);
        } else if (code == -1) {
            /* Keep unsupported UTF-8/code-page glyphs visible as a blank cell
             * rather than interpreting their bytes as engine font indices. */
            if (advance) create_dl_translation_matrix(MENU_MTX_NOPUSH, (f32)advance, 0.0f, 0.0f);
        }
    }
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

static void mod_hud_render_text_command(const struct ModHudCommand *command) {
    gDPPipeSync(gDisplayListHead++);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, command->r, command->g, command->b, command->a);

    size_t length = strlen(command->text);
    size_t line_start = 0;
    unsigned line_index = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || command->text[i] == '\n') {
            mod_hud_render_text_line(command, line_start, i, line_index++);
            line_start = i + 1;
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

static void mod_hud_flush_commands(void) {
    if (sModHudCommandCount == 0) return;
    Mtx *identity = alloc_display_list(sizeof(*identity));
    if (!identity) {
        sModHudCommandCount = 0;
        return;
    }
    guMtxIdent(identity);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(identity), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

    for (unsigned i = 0; i < sModHudCommandCount; ++i) {
        const struct ModHudCommand *command = &sModHudCommands[i];
        if (command->type == MOD_HUD_COMMAND_RECT) mod_hud_render_rect_command(command);
        else if (command->type == MOD_HUD_COMMAND_TEXT) mod_hud_render_text_command(command);
    }
    sModHudCommandCount = 0;
}

/**
 * Renders the text in sTextLabels on screen at the proper locations by iterating
 * a for loop.
 */
void render_text_labels(void) {
    s32 i;
    s32 j;
    s8 glyphIndex;
    Mtx *mtx;

    if (sTextLabelsCount == 0 && sModHudCommandCount == 0) {
        return;
    }

    mtx = alloc_display_list(sizeof(*mtx));

    if (mtx == NULL) {
        sTextLabelsCount = 0;
        sModHudCommandCount = 0;
        sModHudGlyphCount = 0;
        return;
    }

    guOrtho(mtx, 0.0f, SCREEN_WIDTH, 0.0f, SCREEN_HEIGHT, -10.0f, 10.0f, 1.0f);
    gSPPerspNormalize((Gfx *) (gDisplayListHead++), 0xFFFF);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    if (sTextLabelsCount != 0) gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);

    for (i = 0; i < sTextLabelsCount; i++) {
        for (j = 0; j < sTextLabels[i]->length; j++) {
            glyphIndex = char_to_glyph_index(sTextLabels[i]->buffer[j]);

            if (glyphIndex != GLYPH_SPACE) {
#ifdef VERSION_EU
                // Beta Key was removed by EU, so glyph slot reused.
                // This produces a colorful Ü.
                if (glyphIndex == GLYPH_BETA_KEY) {
                    add_glyph_texture(GLYPH_U);
                    render_textrect(sTextLabels[i]->x, sTextLabels[i]->y, j);

                    add_glyph_texture(GLYPH_UMLAUT);
                    render_textrect(sTextLabels[i]->x, sTextLabels[i]->y + 3, j);
                } else {
                    add_glyph_texture(glyphIndex);
                    render_textrect(sTextLabels[i]->x, sTextLabels[i]->y, j);
                }
#else
                add_glyph_texture(glyphIndex);
                render_textrect(sTextLabels[i]->x, sTextLabels[i]->y, j);
#endif
            }
        }

        mem_pool_free(gEffectsMemoryPool, sTextLabels[i]);
    }

    if (sTextLabelsCount != 0) gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    sTextLabelsCount = 0;
    mod_hud_flush_commands();
}
