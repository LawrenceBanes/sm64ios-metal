#include "gfx_texture_override.h"

#include <stdio.h>
#include <string.h>

static bool gfx_hd_asset_name_is_safe(const char *name) {
    if (!name || !*name || name[0] == '/') return false;

    bool segment_has_character = false;
    const char *segment = name;
    for (const unsigned char *p = (const unsigned char *)name;; ++p) {
        unsigned char c = *p;
        if (c == '\\' || c == ':' || (c != '\0' && c < 0x20) || c >= 0x7f) return false;
        if (c == '/' || c == '\0') {
            size_t length = (size_t)((const char *)p - segment);
            if (!segment_has_character ||
                (length == 1 && segment[0] == '.') ||
                (length == 2 && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == '\0') break;
            segment = (const char *)p + 1;
            segment_has_character = false;
        } else {
            segment_has_character = true;
        }
    }
    return true;
}

bool gfx_hd_texture_override_path(const char *asset_name, char *out_path, size_t out_size) {
    if (!out_path || out_size == 0 || !gfx_hd_asset_name_is_safe(asset_name)) return false;
    int written = snprintf(out_path, out_size, "hd_textures/gfx/%s.png", asset_name);
    return written > 0 && (size_t)written < out_size;
}

bool gfx_hd_texture_dimensions_compatible(uint32_t base_width, uint32_t base_height,
                                          uint32_t replacement_width,
                                          uint32_t replacement_height) {
    if (!base_width || !base_height || !replacement_width || !replacement_height ||
        replacement_width < base_width || replacement_height < base_height)
        return false;
    return (uint64_t)base_width * replacement_height ==
           (uint64_t)base_height * replacement_width;
}

bool gfx_hd_texture_budget_allows(uint64_t current_bytes, uint64_t old_texture_bytes,
                                  uint64_t replacement_bytes, uint64_t budget_bytes) {
    if (old_texture_bytes > current_bytes) return false;
    uint64_t without_old = current_bytes - old_texture_bytes;
    return without_old <= budget_bytes && replacement_bytes <= budget_bytes - without_old;
}
