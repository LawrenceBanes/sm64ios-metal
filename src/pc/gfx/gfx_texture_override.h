#ifndef GFX_TEXTURE_OVERRIDE_H
#define GFX_TEXTURE_OVERRIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RENDER96 uses the original SM64 texture path plus a PNG extension. */
bool gfx_hd_texture_override_path(const char *asset_name, char *out_path, size_t out_size);

/* Accept only equal-aspect replacements that do not reduce either dimension. */
bool gfx_hd_texture_dimensions_compatible(uint32_t base_width, uint32_t base_height,
                                         uint32_t replacement_width,
                                         uint32_t replacement_height);

/* Check a replacement against a hard resident-memory budget, accounting for replacement. */
bool gfx_hd_texture_budget_allows(uint64_t current_bytes, uint64_t old_texture_bytes,
                                  uint64_t replacement_bytes, uint64_t budget_bytes);

#endif
