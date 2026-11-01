#ifndef GFX_METAL_STATE_H
#define GFX_METAL_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum GfxSamplerAddressMode {
    GFX_SAMPLER_REPEAT = 0,
    GFX_SAMPLER_MIRROR = 1,
    GFX_SAMPLER_CLAMP = 2,
};

struct GfxMetalSamplerState {
    bool linear;
    uint32_t cms;
    uint32_t cmt;
};

enum GfxMetalPreset {
    GFX_METAL_PRESET_CLASSIC = 0,
    GFX_METAL_PRESET_ENHANCED = 1,
};

struct GfxMetalSettings {
    uint32_t preset;
    uint32_t render_scale_percent;
    uint32_t sample_count;
    uint32_t anisotropy;
    bool hd_textures;
};

struct GfxMetalCapabilities {
    bool sample_count_2;
    bool sample_count_4;
    uint32_t max_anisotropy;
};

struct GfxMetalTargetSize {
    uint32_t width;
    uint32_t height;
};

struct GfxMetalPerformanceStats {
    double cpu_frame_ms;
    double gpu_frame_ms;
    double estimated_fps;
    bool gpu_timing_available;
    uint64_t draw_calls;
    uint64_t triangles;
    uint32_t render_width;
    uint32_t render_height;
    uint32_t preset;
    uint32_t render_scale_percent;
    uint32_t sample_count;
    uint32_t anisotropy;
};

enum GfxSamplerAddressMode gfx_metal_sampler_address_mode(uint32_t n64_mode);
bool gfx_metal_sampler_state_matches(bool current_valid,
                                     struct GfxMetalSamplerState current,
                                     struct GfxMetalSamplerState desired);
uint32_t gfx_metal_pipeline_state_key(bool depth_test, bool depth_write,
                                     bool blend, bool decal);
struct GfxMetalSettings gfx_metal_classic_settings(void);
struct GfxMetalSettings gfx_metal_enhanced_settings(void);
struct GfxMetalSettings gfx_metal_settings_sanitize(
    struct GfxMetalSettings requested, struct GfxMetalCapabilities capabilities);
bool gfx_metal_settings_use_offscreen(struct GfxMetalSettings settings);
bool gfx_metal_target_size(uint32_t drawable_width, uint32_t drawable_height,
                           uint32_t render_scale_percent,
                           struct GfxMetalTargetSize *target_size);
uint32_t gfx_metal_pipeline_cache_index(uint32_t sample_count,
                                        uint32_t render_state_key);

#endif
