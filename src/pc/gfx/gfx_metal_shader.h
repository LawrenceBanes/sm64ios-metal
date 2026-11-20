#ifndef GFX_METAL_SHADER_H
#define GFX_METAL_SHADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_cc.h"

/* Portable shader-source generation: this code has no Apple SDK dependency. */
bool gfx_metal_build_shader(uint32_t shader_id, char *out, size_t out_size,
                            struct CCFeatures *features);
const char *gfx_metal_present_shader_source(void);
const char *gfx_metal_ssao_shader_source(void);
const char *gfx_metal_bloom_shader_source(void);
const char *gfx_metal_shadow_shader_source(void);
const char *gfx_metal_god_ray_shader_source(void);

#endif
