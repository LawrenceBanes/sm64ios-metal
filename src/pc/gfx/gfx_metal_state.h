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
    GFX_METAL_PRESET_CINEMATIC = 2,
    GFX_METAL_PRESET_PERFORMANCE = 3,
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

struct GfxMetalBloomSettings {
    float threshold;
    float intensity;
};

enum GfxMetalColorParameter {
    GFX_METAL_COLOR_BRIGHTNESS = 0,
    GFX_METAL_COLOR_CONTRAST = 1,
    GFX_METAL_COLOR_SATURATION = 2,
    GFX_METAL_COLOR_TEMPERATURE = 3,
};

struct GfxMetalColorAdjustments {
    float brightness;
    float contrast;
    float saturation;
    float temperature;
};

enum GfxMetalAOParameter {
    GFX_METAL_AO_RADIUS = 0,
    GFX_METAL_AO_BIAS = 1,
    GFX_METAL_AO_INTENSITY = 2,
};

struct GfxMetalAOAdjustments {
    float radius_scale;
    float bias_offset;
    float intensity_scale;
};

enum GfxMetalFogParameter {
    GFX_METAL_FOG_DENSITY = 0,
    GFX_METAL_FOG_START = 1,
};

enum GfxMetalToneMappingMode {
    GFX_METAL_TONE_ACES = 0,
    GFX_METAL_TONE_REINHARD = 1,
};

struct GfxMetalFogAdjustments {
    float density_scale;
    float start_scale;
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
    uint64_t ssao_pixels;
    uint64_t bloom_pixels;
    uint64_t shadow_triangles;
    uint64_t god_ray_pixels;
    uint64_t estimated_target_bytes;
};

enum GfxSamplerAddressMode gfx_metal_sampler_address_mode(uint32_t n64_mode);
bool gfx_metal_sampler_state_matches(bool current_valid,
                                     struct GfxMetalSamplerState current,
                                     struct GfxMetalSamplerState desired);
uint32_t gfx_metal_pipeline_state_key(bool depth_test, bool depth_write,
                                     bool blend, bool decal);
struct GfxMetalSettings gfx_metal_classic_settings(void);
struct GfxMetalSettings gfx_metal_enhanced_settings(void);
struct GfxMetalSettings gfx_metal_cinematic_settings(void);
struct GfxMetalSettings gfx_metal_performance_settings(void);
const char *gfx_metal_preset_name(uint32_t preset);
struct GfxMetalSettings gfx_metal_settings_sanitize(
    struct GfxMetalSettings requested, struct GfxMetalCapabilities capabilities);
bool gfx_metal_settings_use_offscreen(struct GfxMetalSettings settings);
bool gfx_metal_target_size(uint32_t drawable_width, uint32_t drawable_height,
                           uint32_t render_scale_percent,
                           struct GfxMetalTargetSize *target_size);
uint32_t gfx_metal_pipeline_cache_index(uint32_t sample_count,
                                        uint32_t render_state_key);
struct GfxMetalBloomSettings gfx_metal_bloom_settings(uint32_t profile);
struct GfxMetalColorAdjustments gfx_metal_color_adjustments_default(void);
struct GfxMetalColorAdjustments gfx_metal_color_adjustments_sanitize(
    struct GfxMetalColorAdjustments adjustments);
struct GfxMetalColorAdjustments gfx_metal_color_adjustments_step(
    struct GfxMetalColorAdjustments adjustments, uint32_t parameter, int direction);
struct GfxMetalAOAdjustments gfx_metal_ao_adjustments_default(void);
struct GfxMetalAOAdjustments gfx_metal_ao_adjustments_sanitize(
    struct GfxMetalAOAdjustments adjustments);
struct GfxMetalAOAdjustments gfx_metal_ao_adjustments_step(
    struct GfxMetalAOAdjustments adjustments, uint32_t parameter, int direction);
struct GfxMetalFogAdjustments gfx_metal_fog_adjustments_default(void);
struct GfxMetalFogAdjustments gfx_metal_fog_adjustments_sanitize(
    struct GfxMetalFogAdjustments adjustments);
struct GfxMetalFogAdjustments gfx_metal_fog_adjustments_step(
    struct GfxMetalFogAdjustments adjustments, uint32_t parameter, int direction);
uint32_t gfx_metal_tone_mapping_sanitize(uint32_t mode);
const char *gfx_metal_tone_mapping_name_for_mode(uint32_t mode);
float gfx_metal_exposure_sanitize(float stops);
float gfx_metal_exposure_step(float stops, int direction);
float gfx_metal_god_ray_intensity_sanitize(float intensity);
float gfx_metal_god_ray_intensity_step(float intensity, int direction);

#endif
