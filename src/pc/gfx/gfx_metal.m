#if defined(__APPLE__)
#import <TargetConditionals.h>
#elif !defined(TARGET_OS_IOS)
#define TARGET_OS_IOS 0
#endif

#if defined(RAPI_METAL) && TARGET_OS_IOS

#import <Metal/Metal.h>
#import <Metal/MTLDrawable.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#import <simd/simd.h>

#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <math.h>
#include "gfx_metal.h"
#include "gfx_metal_state.h"
#include "gfx_metal_shader.h"
#include "gfx_shader64_pack.h"
#include "gfx_metal_vertex_layout.h"
#include "gfx_cc.h"
#include "gfx_scene_capture.h"
#include "gfx_postprocess.h"
#include "gfx_shadow_map.h"
#include "gfx_uikit.h"
#include "../configfile.h"

extern uint32_t gfx_current_render_material_class(void);

#define METAL_SHADER_CAP 128
#define METAL_TEXTURE_CAP 8192
#define METAL_PIPELINE_STATE_CAP 96
#define METAL_SHADOW_VERTEX_LIMIT (8192u * 3u)

struct MetalProgram {
    uint32_t shader_id;
    uint32_t material_generation;
    id<MTLLibrary> library;
    id<MTLFunction> vertex;
    id<MTLFunction> fragment;
    struct CCFeatures features;
    id<MTLRenderPipelineState> pipelines[METAL_PIPELINE_STATE_CAP];
};

struct MetalTexture {
    id<MTLTexture> texture;
    int width;
    int height;
    uint32_t generation;
    bool sampler_state_valid;
    struct GfxMetalSamplerState sampler_state;
};

struct MetalFragmentParams {
    uint32_t filter0;
    uint32_t filter1;
    uint32_t diagnostic_mode;
    uint32_t texture_id0;
    uint32_t texture_id1;
    uint32_t material_class;
    float frame_count;
};

struct MetalShader64FrameInfo {
    float time_seconds;
    float delta_seconds;
    float frame_index;
    float aspect_ratio;
};

struct MetalShader64GraphPass {
    char id[33], input[33], output[33];
    float scale;
    unsigned int node_type;
    unsigned int resource_flags;
    id<MTLRenderPipelineState> pipeline;
    id<MTLComputePipelineState> compute_pipeline;
    id<MTLLibrary> library;
    id<MTLTexture> target;
    NSUInteger target_width, target_height;
};
struct MetalShader64Graph {
    unsigned int references;
    size_t count;
    char *material_source;
    size_t material_source_size;
    bool material_pack_textures;
    unsigned char order[GFX_SHADER64_MAX_GRAPH_PASSES];
    NSUInteger pack_texture_count;
    id<MTLTexture> pack_textures[GFX_SHADER64_MAX_PACK_TEXTURES];
    struct MetalShader64GraphPass passes[GFX_SHADER64_MAX_GRAPH_PASSES];
};

static id<MTLDevice> metal_device;
static id<MTLCommandQueue> metal_queue;
static MTKView *metal_view;
static id<MTLTexture> metal_depth_texture;
static NSUInteger metal_depth_width;
static NSUInteger metal_depth_height;
static NSUInteger metal_depth_samples = 1;
static id<MTLTexture> metal_scene_normals_texture;
static NSUInteger metal_scene_normals_width;
static NSUInteger metal_scene_normals_height;
static bool metal_scene_normals_valid_for_frame;
static id<MTLTexture> metal_color_target;
static id<MTLTexture> metal_multisample_color;
static id<MTLTexture> metal_resolve_texture;
static NSUInteger metal_target_width;
static NSUInteger metal_target_height;
static NSUInteger metal_target_samples;
static id<MTLRenderPipelineState> metal_present_pipeline;
static id<MTLRenderPipelineState> metal_shader64_pipeline;
static id<MTLLibrary> metal_shader64_library;
static struct MetalShader64Graph *metal_shader64_graph;
static char metal_shader64_active_id[65];
static vector_float4 metal_shader64_parameters = { 1.0f, 0.0f, 0.0f, 0.0f };
static uint32_t metal_material_generation = 1;
static id<MTLSamplerState> metal_present_sampler;
static id<MTLComputePipelineState> metal_ssao_pipeline;
static id<MTLComputePipelineState> metal_ssao_denoise_pipeline;
static id<MTLComputePipelineState> metal_scene_normals_pipeline;
static id<MTLComputePipelineState> metal_bloom_extract_pipeline;
static id<MTLComputePipelineState> metal_bloom_blur_pipeline;
static id<MTLTexture> metal_ssao_texture;
static id<MTLTexture> metal_ssao_filtered_texture;
static id<MTLTexture> metal_bloom_extract_texture;
static id<MTLTexture> metal_bloom_temp_texture;
static id<MTLTexture> metal_bloom_output_texture;
static id<MTLTexture> metal_shadow_texture;
static id<MTLTexture> metal_god_ray_texture;
static id<MTLTexture> metal_ui_depth_texture;
static NSUInteger metal_ui_depth_width, metal_ui_depth_height;
static id<MTLRenderPipelineState> metal_shadow_pipeline;
static id<MTLComputePipelineState> metal_god_ray_pipeline;
static id<MTLDepthStencilState> metal_shadow_depth_state;
static id<MTLBuffer> metal_shadow_vertex_buffer;
static float (*metal_shadow_world_positions)[3];
static struct GfxShadowMapTransform metal_shadow_transform;
static bool metal_shadow_transform_valid;
static bool metal_shadow_cache_valid;
static uint64_t metal_shadow_cached_geometry_key;
static size_t metal_shadow_cached_vertex_count;
static NSUInteger metal_shadow_texture_size;
static NSUInteger metal_god_ray_width, metal_god_ray_height;
static NSUInteger metal_ssao_width, metal_ssao_height;
static NSUInteger metal_bloom_width, metal_bloom_height;
static bool metal_ssao_is_enabled;
static uint32_t metal_ssao_debug;
static uint32_t metal_ssao_quality;
static struct GfxMetalAOAdjustments metal_ao_adjustments = { 1.0f, 0.0f, 1.0f };
static bool metal_post_enabled;
static uint32_t metal_tone_mapping_mode;
static bool metal_fxaa_enabled;
static bool metal_effects_enabled;
static float metal_exposure_stops;
static uint32_t metal_color_grade;
static struct GfxMetalColorAdjustments metal_color_adjustments = { 0.0f, 1.0f, 1.0f, 0.0f };
static bool metal_fog_is_enabled;
static uint32_t metal_fog_profile;
static struct GfxMetalFogAdjustments metal_fog_adjustments = { 1.0f, 1.0f };
static uint32_t metal_sky_preset;
static bool metal_bloom_is_enabled;
static uint32_t metal_bloom_profile = 1u;
static bool metal_shadows_is_enabled;
static uint32_t metal_shadow_resolution = 512u;
static uint32_t metal_sun_preset;
static uint32_t metal_sun_intensity;
static bool metal_god_rays_is_enabled;
static uint32_t metal_god_ray_quality = 1u;
static float metal_god_ray_intensity = 1.0f;
static struct GfxMetalSettings metal_settings = { GFX_METAL_PRESET_CLASSIC, 100u, 1u, 1u, false };
static struct GfxMetalCapabilities metal_capabilities = { false, false, 1u };
static struct GfxMetalPerformanceStats metal_performance_stats;
static double metal_last_frame_start_time;
static bool metal_logged_gpu_timing;
static bool metal_logged_gpu_timing_unavailable;
static uint64_t metal_frame_draw_calls;
static uint64_t metal_frame_triangles;
static NSUInteger metal_active_sample_count = 1;
static NSUInteger metal_active_render_width;
static NSUInteger metal_active_render_height;
static bool metal_frame_uses_offscreen;
static bool metal_scene_postprocessed_before_ui;
static bool metal_offscreen_runtime_failed;
static void gfx_metal_restore_shader64_pack(void);
static bool gfx_metal_shader64_is_active(void);
static bool gfx_metal_shader64_requires_shadows(void);
static id<MTLCommandBuffer> metal_command;
static id<MTLRenderCommandEncoder> metal_encoder;
static id<CAMetalDrawable> metal_drawable;
static struct MetalProgram programs[METAL_SHADER_CAP];
static size_t program_count;
static struct MetalTexture textures[METAL_TEXTURE_CAP];
static size_t texture_count;
static uint32_t frame_count;
static double shader64_start_time;
static double shader64_previous_time;
static bool metal_scene_active;
static bool logged_first_completed_frame;
static bool logged_first_presented_frame;
static bool logged_first_triangle_draw;
static bool logged_portal_draw[2];
static uint64_t presentation_callback_count;
static uint64_t displayed_drawable_count;
static uint64_t completed_drawable_count;
static uint64_t scheduled_command_count;
static uint64_t acquired_drawable_count;
static uintptr_t last_acquired_drawable_texture_identity;
static uint64_t repeated_drawable_texture_count;
static bool log_next_draw_rects;
static uint32_t drawable_unavailable_count;
static uint32_t inactive_scene_frame_count;
static uint32_t logged_pipeline_failures;
static uint32_t logged_pipeline_successes;
static uint32_t logged_draw_warnings;
static struct MetalTexture *bound_textures[2];
static struct MetalTexture *current_texture;
static id<MTLSamplerState> metal_samplers[2];
static NSUInteger sampler_anisotropy[2] = { 1, 1 };
static bool sampler_linear[2];
static uint32_t sampler_cms[2];
static uint32_t sampler_cmt[2];
static bool sampler_state_valid[2];
static uint64_t sampler_restore_count;
static struct MetalProgram *active_program;
static bool state_depth_test = true;
static bool state_depth_write = true;
static bool state_blend;
static bool state_decal;
static int viewport[4];
static int scissor[4];
static bool has_viewport;
static bool has_scissor;
static id<MTLDepthStencilState> depth_states[2][2];
static uint64_t metal_draw_batch_count;
static uint64_t texture_upload_snapshot_count;

static bool gfx_metal_diagnostics_enabled(void) {
    return gfx_ios_texture_cache_diagnostics_enabled() ||
           gfx_ios_render_diagnostic_mode() != GFX_IOS_RENDER_NORMAL;
}

static id<MTLTexture> gfx_metal_depth_for_size(NSUInteger width, NSUInteger height,
                                               NSUInteger sample_count) {
    if (metal_depth_texture && metal_depth_width == width && metal_depth_height == height &&
        metal_depth_samples == sample_count)
        return metal_depth_texture;

    [metal_depth_texture release];
    metal_depth_texture = nil;
    metal_depth_width = metal_depth_height = 0;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                          width:width
                                                                                         height:height
                                                                                      mipmapped:NO];
    descriptor.textureType = sample_count > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    descriptor.sampleCount = sample_count;
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    metal_depth_texture = [metal_device newTextureWithDescriptor:descriptor];
    if (!metal_depth_texture) {
        gfx_ios_diag_log("Metal depth texture creation failed drawable=%lux%lu format=Depth32Float", (unsigned long)width, (unsigned long)height);
        return nil;
    }
    metal_depth_width = width;
    metal_depth_height = height;
    metal_depth_samples = sample_count;
    metal_depth_texture.label = @"SM64 game depth";
    gfx_ios_diag_log("Metal depth texture ready size=%lux%lu samples=%lu format=Depth32Float", (unsigned long)width, (unsigned long)height, (unsigned long)sample_count);
    return metal_depth_texture;
}

static void gfx_metal_apply_sampler_parameters(int sampler, bool linear,
                                               uint32_t cms, uint32_t cmt);

static void gfx_metal_release_render_targets(void) {
    [metal_color_target release]; metal_color_target = nil;
    [metal_multisample_color release]; metal_multisample_color = nil;
    [metal_resolve_texture release]; metal_resolve_texture = nil;
    [metal_depth_texture release]; metal_depth_texture = nil;
    [metal_scene_normals_texture release]; metal_scene_normals_texture = nil;
    metal_scene_normals_width = metal_scene_normals_height = 0;
    metal_scene_normals_valid_for_frame = false;
    [metal_ssao_texture release]; metal_ssao_texture = nil;
    [metal_ssao_filtered_texture release]; metal_ssao_filtered_texture = nil;
    [metal_bloom_extract_texture release]; metal_bloom_extract_texture = nil;
    [metal_bloom_temp_texture release]; metal_bloom_temp_texture = nil;
    [metal_bloom_output_texture release]; metal_bloom_output_texture = nil;
    [metal_shadow_texture release]; metal_shadow_texture = nil;
    [metal_god_ray_texture release]; metal_god_ray_texture = nil;
    [metal_ui_depth_texture release]; metal_ui_depth_texture = nil;
    metal_ui_depth_width = metal_ui_depth_height = 0;
    metal_god_ray_width = metal_god_ray_height = 0;
    metal_shadow_texture_size = 0;
    metal_shadow_transform_valid = false;
    metal_shadow_cache_valid = false;
    metal_shadow_cached_geometry_key = 0;
    metal_shadow_cached_vertex_count = 0;
    metal_ssao_width = metal_ssao_height = 0;
    metal_bloom_width = metal_bloom_height = 0;
    metal_depth_width = metal_depth_height = 0;
    metal_depth_samples = 1;
    metal_target_width = metal_target_height = metal_target_samples = 0;
    @synchronized ([NSObject class]) {
        metal_performance_stats.ssao_pixels = 0;
        metal_performance_stats.bloom_pixels = 0;
        metal_performance_stats.shadow_triangles = 0;
        metal_performance_stats.god_ray_pixels = 0;
        metal_performance_stats.estimated_target_bytes = 0;
    }
}

static void gfx_metal_refresh_capabilities(void) {
    if (!metal_device) return;
    metal_capabilities.sample_count_2 = [metal_device supportsTextureSampleCount:2];
    metal_capabilities.sample_count_4 = [metal_device supportsTextureSampleCount:4];
    metal_capabilities.max_anisotropy = 1;
    static const NSUInteger levels[] = { 16, 8, 4, 2, 1 };
    for (NSUInteger i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        MTLSamplerDescriptor *descriptor = [[[MTLSamplerDescriptor alloc] init] autorelease];
        descriptor.minFilter = MTLSamplerMinMagFilterLinear;
        descriptor.magFilter = MTLSamplerMinMagFilterLinear;
        descriptor.sAddressMode = MTLSamplerAddressModeRepeat;
        descriptor.tAddressMode = MTLSamplerAddressModeRepeat;
        descriptor.maxAnisotropy = levels[i];
        id<MTLSamplerState> sampler = [metal_device newSamplerStateWithDescriptor:descriptor];
        if (sampler) {
            metal_capabilities.max_anisotropy = (uint32_t)levels[i];
            [sampler release];
            break;
        }
    }
    gfx_ios_diag_log("Metal capabilities device=%s sampleCounts=(1,%d,%d) samplerMaxAnisotropy=%u", metal_device.name.UTF8String ?: "unknown", metal_capabilities.sample_count_2, metal_capabilities.sample_count_4, metal_capabilities.max_anisotropy);
}

static struct GfxMetalSettings gfx_metal_read_persisted_settings(void) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    if ([defaults objectForKey:@"SM64MetalSSAOMode"])
        metal_ssao_debug = (uint32_t)MIN(3, MAX(0, [defaults integerForKey:@"SM64MetalSSAOMode"]));
    else
        metal_ssao_debug = [defaults boolForKey:@"SM64MetalSSAOEnabled"] ? 1u : 0u;
    metal_ssao_is_enabled = metal_ssao_debug != 0u;
    metal_ssao_quality = (uint32_t)MIN(2, MAX(0, [defaults integerForKey:@"SM64MetalSSAOQuality"]));
    metal_ao_adjustments = gfx_metal_ao_adjustments_sanitize((struct GfxMetalAOAdjustments){
        [defaults objectForKey:@"SM64MetalAORadius"] ? (float)[defaults doubleForKey:@"SM64MetalAORadius"] : 1.0f,
        (float)[defaults doubleForKey:@"SM64MetalAOBias"],
        [defaults objectForKey:@"SM64MetalAOIntensity"] ? (float)[defaults doubleForKey:@"SM64MetalAOIntensity"] : 1.0f
    });
    metal_post_enabled = [defaults boolForKey:@"SM64MetalPostEnabled"];
    metal_tone_mapping_mode = (uint32_t)MIN(1, MAX(0, [defaults integerForKey:@"SM64MetalToneMappingMode"]));
    metal_fxaa_enabled = [defaults boolForKey:@"SM64MetalFXAAEnabled"];
    metal_effects_enabled = ![defaults objectForKey:@"SM64MetalEffectsEnabled"] ||
                            [defaults boolForKey:@"SM64MetalEffectsEnabled"];
    metal_shadows_is_enabled = [defaults boolForKey:@"SM64MetalShadowsEnabled"];
    metal_god_rays_is_enabled = [defaults boolForKey:@"SM64MetalGodRaysEnabled"];
    NSInteger saved_ray_quality = [defaults integerForKey:@"SM64MetalGodRayQuality"];
    metal_god_ray_quality = [defaults objectForKey:@"SM64MetalGodRayQuality"] ?
        (uint32_t)MIN(2, MAX(0, saved_ray_quality)) : 1u;
    metal_god_ray_intensity = gfx_metal_god_ray_intensity_sanitize(
        [defaults objectForKey:@"SM64MetalGodRayIntensity"] ? (float)[defaults doubleForKey:@"SM64MetalGodRayIntensity"] : 1.0f);
    NSInteger saved_shadow_resolution = [defaults integerForKey:@"SM64MetalShadowResolution"];
    metal_shadow_resolution = saved_shadow_resolution == 1024 || saved_shadow_resolution == 2048 ?
                              (uint32_t)saved_shadow_resolution : 512u;
    metal_sun_preset = (uint32_t)MIN(3, MAX(0, [defaults integerForKey:@"SM64MetalSunPreset"]));
    metal_sun_intensity = (uint32_t)MIN(3, MAX(0, [defaults integerForKey:@"SM64MetalSunIntensity"]));
    metal_exposure_stops = gfx_metal_exposure_sanitize((float)[defaults doubleForKey:@"SM64MetalExposureStops"]);
    metal_color_grade = (uint32_t)MIN(3, MAX(0, [defaults integerForKey:@"SM64MetalColorGrade"]));
    metal_color_adjustments = gfx_metal_color_adjustments_sanitize((struct GfxMetalColorAdjustments){
        (float)[defaults doubleForKey:@"SM64MetalBrightness"],
        [defaults objectForKey:@"SM64MetalContrast"] ? (float)[defaults doubleForKey:@"SM64MetalContrast"] : 1.0f,
        [defaults objectForKey:@"SM64MetalSaturation"] ? (float)[defaults doubleForKey:@"SM64MetalSaturation"] : 1.0f,
        (float)[defaults doubleForKey:@"SM64MetalTemperature"]
    });
    metal_fog_is_enabled = [defaults boolForKey:@"SM64MetalFogEnabled"];
    metal_fog_profile = (uint32_t)MIN(3, MAX(0, [defaults integerForKey:@"SM64MetalFogProfile"]));
    metal_fog_adjustments = gfx_metal_fog_adjustments_sanitize((struct GfxMetalFogAdjustments){
        [defaults objectForKey:@"SM64MetalFogDensity"] ? (float)[defaults doubleForKey:@"SM64MetalFogDensity"] : 1.0f,
        [defaults objectForKey:@"SM64MetalFogStart"] ? (float)[defaults doubleForKey:@"SM64MetalFogStart"] : 1.0f
    });
    metal_sky_preset = (uint32_t)MIN(2, MAX(0, [defaults integerForKey:@"SM64MetalSkyPreset"]));
    metal_bloom_is_enabled = [defaults boolForKey:@"SM64MetalBloomEnabled"];
    if ([defaults objectForKey:@"SM64MetalBloomProfile"])
        metal_bloom_profile = (uint32_t)MIN(2, MAX(0, [defaults integerForKey:@"SM64MetalBloomProfile"]));
    struct GfxMetalSettings requested = gfx_metal_classic_settings();
    if ([defaults objectForKey:@"SM64MetalPreset"]) {
        requested.preset = (uint32_t)[defaults integerForKey:@"SM64MetalPreset"];
        switch (requested.preset) {
            case GFX_METAL_PRESET_ENHANCED: requested = gfx_metal_enhanced_settings(); break;
            case GFX_METAL_PRESET_CINEMATIC: requested = gfx_metal_cinematic_settings(); break;
            case GFX_METAL_PRESET_PERFORMANCE: requested = gfx_metal_performance_settings(); break;
            default: requested = gfx_metal_classic_settings(); break;
        }
    }
    if ([defaults objectForKey:@"SM64MetalRenderScale"])
        requested.render_scale_percent = (uint32_t)[defaults integerForKey:@"SM64MetalRenderScale"];
    if ([defaults objectForKey:@"SM64MetalSampleCount"])
        requested.sample_count = (uint32_t)[defaults integerForKey:@"SM64MetalSampleCount"];
    if ([defaults objectForKey:@"SM64MetalAnisotropy"])
        requested.anisotropy = (uint32_t)[defaults integerForKey:@"SM64MetalAnisotropy"];
    if ([defaults objectForKey:@"SM64MetalHDTextures"])
        requested.hd_textures = [defaults boolForKey:@"SM64MetalHDTextures"];
    return gfx_metal_settings_sanitize(requested, metal_capabilities);
}

static void gfx_metal_persist_settings(struct GfxMetalSettings settings) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:(NSInteger)settings.preset forKey:@"SM64MetalPreset"];
    [defaults setInteger:(NSInteger)settings.render_scale_percent forKey:@"SM64MetalRenderScale"];
    [defaults setInteger:(NSInteger)settings.sample_count forKey:@"SM64MetalSampleCount"];
    [defaults setInteger:(NSInteger)settings.anisotropy forKey:@"SM64MetalAnisotropy"];
    [defaults setBool:settings.hd_textures forKey:@"SM64MetalHDTextures"];
}

bool gfx_metal_hd_textures_enabled(void) {
    @synchronized ([NSObject class]) {
        return metal_settings.preset != GFX_METAL_PRESET_CLASSIC && metal_settings.hd_textures;
    }
}

bool gfx_metal_ssao_enabled(void) {
    @synchronized ([NSObject class]) { return metal_effects_enabled && metal_ssao_is_enabled; }
}

void gfx_metal_set_ssao_enabled(bool enabled) {
    @synchronized ([NSObject class]) {
        metal_ssao_debug = enabled ? 1u : 0u;
        metal_ssao_is_enabled = enabled;
        metal_offscreen_runtime_failed = false;
        if (!enabled) metal_performance_stats.ssao_pixels = 0;
    }
    [[NSUserDefaults standardUserDefaults] setInteger:enabled ? 1 : 0 forKey:@"SM64MetalSSAOMode"];
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalSSAOEnabled"];
    gfx_ios_diag_log("Metal SSAO %s", enabled ? "enabled" : "disabled");
}

uint32_t gfx_metal_ssao_debug_mode(void) {
    @synchronized ([NSObject class]) { return metal_effects_enabled ? metal_ssao_debug : 0u; }
}

void gfx_metal_cycle_ssao_mode(void) {
    @synchronized ([NSObject class]) {
        metal_ssao_debug = (metal_ssao_debug + 1u) % 4u;
        metal_ssao_is_enabled = metal_ssao_debug != 0u;
        metal_offscreen_runtime_failed = false;
        if (!metal_ssao_is_enabled) metal_performance_stats.ssao_pixels = 0;
    }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)metal_ssao_debug forKey:@"SM64MetalSSAOMode"];
    [[NSUserDefaults standardUserDefaults] setBool:metal_ssao_debug != 0u forKey:@"SM64MetalSSAOEnabled"];
    gfx_ios_diag_log("Metal SSAO display mode=%u", metal_ssao_debug);
}
uint32_t gfx_metal_ssao_quality(void) { @synchronized ([NSObject class]) { return metal_ssao_quality; } }
void gfx_metal_set_ssao_quality(uint32_t quality) {
    uint32_t sanitized = MIN(quality, 2u);
    @synchronized ([NSObject class]) { metal_ssao_quality = sanitized; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalSSAOQuality"];
}
void gfx_metal_cycle_ssao_quality(void) {
    gfx_metal_set_ssao_quality((gfx_metal_ssao_quality() + 1u) % 3u);
}
struct GfxMetalAOAdjustments gfx_metal_get_ao_adjustments(void) {
    @synchronized ([NSObject class]) { return metal_ao_adjustments; }
}
void gfx_metal_adjust_ao(uint32_t parameter, int direction) {
    struct GfxMetalAOAdjustments value;
    @synchronized ([NSObject class]) {
        metal_ao_adjustments = gfx_metal_ao_adjustments_step(metal_ao_adjustments, parameter, direction);
        metal_offscreen_runtime_failed = false;
        value = metal_ao_adjustments;
    }
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setDouble:value.radius_scale forKey:@"SM64MetalAORadius"];
    [defaults setDouble:value.bias_offset forKey:@"SM64MetalAOBias"];
    [defaults setDouble:value.intensity_scale forKey:@"SM64MetalAOIntensity"];
}
void gfx_metal_reset_ao_adjustments(void) {
    @synchronized ([NSObject class]) {
        metal_ao_adjustments = gfx_metal_ao_adjustments_default();
        metal_offscreen_runtime_failed = false;
    }
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setDouble:1.0 forKey:@"SM64MetalAORadius"];
    [defaults setDouble:0.0 forKey:@"SM64MetalAOBias"];
    [defaults setDouble:1.0 forKey:@"SM64MetalAOIntensity"];
}

bool gfx_metal_effects_enabled(void) { @synchronized ([NSObject class]) { return metal_effects_enabled; } }
bool gfx_metal_offscreen_fallback_active(void) {
    @synchronized ([NSObject class]) { return metal_offscreen_runtime_failed; }
}
void gfx_metal_set_effects_enabled(bool enabled) {
    @synchronized ([NSObject class]) {
        metal_effects_enabled = enabled;
        metal_offscreen_runtime_failed = false;
        if (!enabled) {
            metal_performance_stats.ssao_pixels = 0;
            metal_performance_stats.bloom_pixels = 0;
            metal_performance_stats.shadow_triangles = 0;
            metal_performance_stats.god_ray_pixels = 0;
        }
    }
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalEffectsEnabled"];
}
bool gfx_metal_shadows_enabled(void) {
    bool configured;
    @synchronized ([NSObject class]) { configured = metal_effects_enabled && metal_shadows_is_enabled; }
    return configured || gfx_metal_shader64_requires_shadows();
}
void gfx_metal_set_shadows_enabled(bool enabled) {
    @synchronized ([NSObject class]) {
        metal_shadows_is_enabled = enabled;
        metal_offscreen_runtime_failed = false;
        if (!enabled) metal_performance_stats.shadow_triangles = 0;
    }
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalShadowsEnabled"];
    gfx_metal_release_render_targets();
}
uint32_t gfx_metal_shadow_resolution(void) { @synchronized ([NSObject class]) { return metal_shadow_resolution; } }
void gfx_metal_set_shadow_resolution(uint32_t resolution) {
    uint32_t sanitized = resolution == 1024u || resolution == 2048u ? resolution : 512u;
    @synchronized ([NSObject class]) {
        if (metal_shadow_resolution == sanitized) return;
        metal_shadow_resolution = sanitized;
        gfx_metal_release_render_targets();
    }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalShadowResolution"];
}
void gfx_metal_cycle_shadow_resolution(void) {
    uint32_t next = gfx_metal_shadow_resolution() == 512u ? 1024u :
                    (gfx_metal_shadow_resolution() == 1024u ? 2048u : 512u);
    @synchronized ([NSObject class]) {
        metal_shadow_resolution = next;
        metal_offscreen_runtime_failed = false;
    }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)next forKey:@"SM64MetalShadowResolution"];
    [metal_shadow_texture release]; metal_shadow_texture = nil;
    metal_shadow_texture_size = 0;
}
uint32_t gfx_metal_sun_preset(void) { @synchronized ([NSObject class]) { return metal_sun_preset; } }
void gfx_metal_set_sun_preset(uint32_t preset) {
    uint32_t sanitized = MIN(preset, 3u);
    @synchronized ([NSObject class]) { metal_sun_preset = sanitized; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalSunPreset"];
}
void gfx_metal_cycle_sun_preset(void) {
    gfx_metal_set_sun_preset((gfx_metal_sun_preset() + 1u) % 4u);
}
void gfx_metal_get_sun_direction(float direction[3]) {
    if (!direction) return;
    static const float directions[4][3] = {
        { 0.35f, 0.93f, 0.25f }, { -0.45f, 0.87f, 0.20f },
        { 0.15f, 0.97f, -0.18f }, { 0.50f, 0.71f, -0.49f }
    };
    @synchronized ([NSObject class]) {
        memcpy(direction, directions[MIN(metal_sun_preset, 3u)], 3u * sizeof(float));
    }
}
const char *gfx_metal_sun_preset_name(void) {
    static const char *const names[] = { "Afternoon", "Morning", "Noon", "Sunset" };
    return names[gfx_metal_sun_preset() % 4u];
}
uint32_t gfx_metal_sun_intensity(void) {
    @synchronized ([NSObject class]) { return metal_effects_enabled ? metal_sun_intensity : 0u; }
}
void gfx_metal_set_sun_intensity(uint32_t intensity) {
    uint32_t sanitized = MIN(intensity, 3u);
    @synchronized ([NSObject class]) { metal_sun_intensity = sanitized; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalSunIntensity"];
}
void gfx_metal_cycle_sun_intensity(void) {
    gfx_metal_set_sun_intensity((gfx_metal_sun_intensity() + 1u) % 4u);
}
bool gfx_metal_god_rays_enabled(void) {
    @synchronized ([NSObject class]) { return metal_effects_enabled && metal_god_rays_is_enabled; }
}
void gfx_metal_set_god_rays_enabled(bool enabled) {
    @synchronized ([NSObject class]) {
        metal_god_rays_is_enabled = enabled;
        metal_offscreen_runtime_failed = false;
        if (!enabled) metal_performance_stats.god_ray_pixels = 0;
    }
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalGodRaysEnabled"];
    gfx_metal_release_render_targets();
}
uint32_t gfx_metal_god_ray_quality(void) {
    @synchronized ([NSObject class]) { return metal_god_ray_quality; }
}
float gfx_metal_god_ray_intensity(void) {
    @synchronized ([NSObject class]) { return metal_god_ray_intensity; }
}
void gfx_metal_adjust_god_ray_intensity(int direction) {
    float intensity;
    @synchronized ([NSObject class]) {
        metal_god_ray_intensity = gfx_metal_god_ray_intensity_step(metal_god_ray_intensity, direction);
        metal_offscreen_runtime_failed = false;
        intensity = metal_god_ray_intensity;
    }
    [[NSUserDefaults standardUserDefaults] setDouble:intensity forKey:@"SM64MetalGodRayIntensity"];
}
void gfx_metal_reset_god_ray_intensity(void) {
    @synchronized ([NSObject class]) { metal_god_ray_intensity = 1.0f; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setDouble:1.0 forKey:@"SM64MetalGodRayIntensity"];
}
void gfx_metal_set_god_ray_quality(uint32_t quality) {
    uint32_t sanitized = MIN(quality, 2u);
    @synchronized ([NSObject class]) { metal_god_ray_quality = sanitized; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalGodRayQuality"];
}
void gfx_metal_cycle_god_ray_quality(void) {
    gfx_metal_set_god_ray_quality((gfx_metal_god_ray_quality() + 1u) % 3u);
}
bool gfx_metal_post_enabled(void) { @synchronized ([NSObject class]) { return metal_effects_enabled && metal_post_enabled; } }
void gfx_metal_set_post_enabled(bool enabled) {
    @synchronized ([NSObject class]) { metal_post_enabled = enabled; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalPostEnabled"];
}
uint32_t gfx_metal_tone_mapping_mode(void) {
    @synchronized ([NSObject class]) { return metal_tone_mapping_mode; }
}
void gfx_metal_set_tone_mapping_mode(uint32_t mode) {
    uint32_t sanitized = gfx_metal_tone_mapping_sanitize(mode);
    @synchronized ([NSObject class]) { metal_tone_mapping_mode = sanitized; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalToneMappingMode"];
}
const char *gfx_metal_tone_mapping_name(void) {
    return gfx_metal_tone_mapping_name_for_mode(gfx_metal_tone_mapping_mode());
}
bool gfx_metal_fxaa_enabled(void) { @synchronized ([NSObject class]) { return metal_effects_enabled && metal_fxaa_enabled; } }
void gfx_metal_set_fxaa_enabled(bool enabled) {
    @synchronized ([NSObject class]) { metal_fxaa_enabled = enabled; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalFXAAEnabled"];
}
float gfx_metal_exposure_stops(void) { @synchronized ([NSObject class]) { return metal_effects_enabled ? metal_exposure_stops : 0.0f; } }
void gfx_metal_step_exposure(void) {
    gfx_metal_adjust_exposure(1);
}
void gfx_metal_adjust_exposure(int direction) {
    @synchronized ([NSObject class]) {
        metal_exposure_stops = gfx_metal_exposure_step(metal_exposure_stops, direction);
        metal_offscreen_runtime_failed = false;
    }
    [[NSUserDefaults standardUserDefaults] setDouble:metal_exposure_stops forKey:@"SM64MetalExposureStops"];
}
void gfx_metal_reset_exposure(void) {
    @synchronized ([NSObject class]) { metal_exposure_stops = 0.0f; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setDouble:0.0 forKey:@"SM64MetalExposureStops"];
}
uint32_t gfx_metal_color_grade(void) { @synchronized ([NSObject class]) { return metal_effects_enabled ? metal_color_grade : 0u; } }
void gfx_metal_cycle_color_grade(void) {
    gfx_metal_set_color_grade((gfx_metal_color_grade() + 1u) % 4u);
}
void gfx_metal_set_color_grade(uint32_t grade) {
    @synchronized ([NSObject class]) { metal_color_grade = MIN(grade, 3u); metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)metal_color_grade forKey:@"SM64MetalColorGrade"];
}
struct GfxMetalColorAdjustments gfx_metal_get_color_adjustments(void) {
    @synchronized ([NSObject class]) { return metal_color_adjustments; }
}
void gfx_metal_adjust_color(uint32_t parameter, int direction) {
    struct GfxMetalColorAdjustments value;
    @synchronized ([NSObject class]) {
        metal_color_adjustments = gfx_metal_color_adjustments_step(metal_color_adjustments, parameter, direction);
        metal_offscreen_runtime_failed = false;
        value = metal_color_adjustments;
    }
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setDouble:value.brightness forKey:@"SM64MetalBrightness"];
    [defaults setDouble:value.contrast forKey:@"SM64MetalContrast"];
    [defaults setDouble:value.saturation forKey:@"SM64MetalSaturation"];
    [defaults setDouble:value.temperature forKey:@"SM64MetalTemperature"];
}
void gfx_metal_reset_color_adjustments(void) {
    @synchronized ([NSObject class]) {
        metal_color_adjustments = gfx_metal_color_adjustments_default();
        metal_offscreen_runtime_failed = false;
    }
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setDouble:0.0 forKey:@"SM64MetalBrightness"];
    [defaults setDouble:1.0 forKey:@"SM64MetalContrast"];
    [defaults setDouble:1.0 forKey:@"SM64MetalSaturation"];
    [defaults setDouble:0.0 forKey:@"SM64MetalTemperature"];
}
bool gfx_metal_fog_enabled(void) { @synchronized ([NSObject class]) { return metal_effects_enabled && metal_fog_is_enabled; } }
void gfx_metal_set_fog_enabled(bool enabled) {
    @synchronized ([NSObject class]) { metal_fog_is_enabled = enabled; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalFogEnabled"];
}
uint32_t gfx_metal_fog_profile(void) { @synchronized ([NSObject class]) { return metal_fog_profile; } }
void gfx_metal_cycle_fog_profile(void) {
    gfx_metal_set_fog_profile((gfx_metal_fog_profile() + 1u) % 4u);
}
void gfx_metal_set_fog_profile(uint32_t profile) {
    uint32_t sanitized = MIN(profile, 3u);
    @synchronized ([NSObject class]) { metal_fog_profile = sanitized; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalFogProfile"];
}
struct GfxMetalFogAdjustments gfx_metal_get_fog_adjustments(void) {
    @synchronized ([NSObject class]) { return metal_fog_adjustments; }
}
void gfx_metal_adjust_fog(uint32_t parameter, int direction) {
    struct GfxMetalFogAdjustments value;
    @synchronized ([NSObject class]) {
        metal_fog_adjustments = gfx_metal_fog_adjustments_step(metal_fog_adjustments, parameter, direction);
        metal_offscreen_runtime_failed = false;
        value = metal_fog_adjustments;
    }
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setDouble:value.density_scale forKey:@"SM64MetalFogDensity"];
    [defaults setDouble:value.start_scale forKey:@"SM64MetalFogStart"];
}
void gfx_metal_reset_fog_adjustments(void) {
    @synchronized ([NSObject class]) {
        metal_fog_adjustments = gfx_metal_fog_adjustments_default();
        metal_offscreen_runtime_failed = false;
    }
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setDouble:1.0 forKey:@"SM64MetalFogDensity"];
    [defaults setDouble:1.0 forKey:@"SM64MetalFogStart"];
}
uint32_t gfx_metal_sky_preset(void) { @synchronized ([NSObject class]) { return metal_effects_enabled ? metal_sky_preset : 0u; } }
void gfx_metal_cycle_sky_preset(void) {
    gfx_metal_set_sky_preset((gfx_metal_sky_preset() + 1u) % 3u);
}
void gfx_metal_set_sky_preset(uint32_t preset) {
    uint32_t sanitized = MIN(preset, 2u);
    @synchronized ([NSObject class]) { metal_sky_preset = sanitized; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalSkyPreset"];
}
bool gfx_metal_bloom_enabled(void) { @synchronized ([NSObject class]) { return metal_effects_enabled && metal_bloom_is_enabled; } }
void gfx_metal_set_bloom_enabled(bool enabled) {
    @synchronized ([NSObject class]) {
        metal_bloom_is_enabled = enabled;
        metal_offscreen_runtime_failed = false;
        if (!enabled) metal_performance_stats.bloom_pixels = 0;
    }
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64MetalBloomEnabled"];
}
uint32_t gfx_metal_bloom_profile(void) {
    @synchronized ([NSObject class]) { return metal_bloom_profile; }
}
void gfx_metal_set_bloom_profile(uint32_t profile) {
    uint32_t sanitized = MIN(profile, 2u);
    @synchronized ([NSObject class]) { metal_bloom_profile = sanitized; metal_offscreen_runtime_failed = false; }
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)sanitized forKey:@"SM64MetalBloomProfile"];
}

struct GfxMetalSettings gfx_metal_get_settings(void) {
    @synchronized ([NSObject class]) { return metal_settings; }
}

struct GfxMetalCapabilities gfx_metal_get_capabilities(void) {
    @synchronized ([NSObject class]) { return metal_capabilities; }
}

void gfx_metal_apply_settings(struct GfxMetalSettings requested) {
    struct GfxMetalSettings sanitized = gfx_metal_settings_sanitize(requested, metal_capabilities);
    @synchronized ([NSObject class]) {
        bool targets_changed = metal_settings.render_scale_percent != sanitized.render_scale_percent ||
                               metal_settings.sample_count != sanitized.sample_count;
        bool changed = metal_settings.preset != sanitized.preset ||
                       metal_settings.render_scale_percent != sanitized.render_scale_percent ||
                       metal_settings.sample_count != sanitized.sample_count ||
                       metal_settings.anisotropy != sanitized.anisotropy ||
                       metal_settings.hd_textures != sanitized.hd_textures;
        metal_settings = sanitized;
        if (targets_changed || !gfx_metal_settings_use_offscreen(sanitized))
            metal_offscreen_runtime_failed = false;
        metal_performance_stats.preset = sanitized.preset;
        metal_performance_stats.render_scale_percent = sanitized.render_scale_percent;
        metal_performance_stats.sample_count = sanitized.sample_count;
        metal_performance_stats.anisotropy = sanitized.anisotropy;
        if (targets_changed) gfx_metal_release_render_targets();
        if (changed) {
            gfx_ios_diag_log("Metal graphics settings applied preset=%s scale=%u%% samples=%ux anisotropy=%u hdTextures=%d", gfx_metal_preset_name(sanitized.preset), sanitized.render_scale_percent, sanitized.sample_count, sanitized.anisotropy, sanitized.hd_textures);
        }
    }
    gfx_metal_persist_settings(sanitized);
    for (int tile = 0; tile < 2; ++tile) {
        if (metal_device)
            gfx_metal_apply_sampler_parameters(tile, sampler_linear[tile],
                                               sampler_cms[tile], sampler_cmt[tile]);
    }
}

void gfx_metal_get_performance_stats(struct GfxMetalPerformanceStats *stats) {
    if (!stats) return;
    @synchronized ([NSObject class]) { *stats = metal_performance_stats; }
}

void gfx_metal_set_cpu_frame_time_ms(double milliseconds) {
    if (milliseconds < 0.0) return;
    @synchronized ([NSObject class]) { metal_performance_stats.cpu_frame_ms = milliseconds; }
}

static void gfx_metal_on_resize(void);

static bool gfx_metal_z_is_from_0_to_1(void) { return true; }

bool gfx_metal_surface_is_ready(void) {
    if (!metal_device || !metal_queue || !metal_view || !metal_view.window ||
        !metal_view.window.windowScene || metal_view.hidden || metal_view.window.hidden || metal_view.alpha <= 0.0) {
        return false;
    }
    CGSize drawable = metal_view.drawableSize;
    return drawable.width > 0.0 && drawable.height > 0.0;
}

void gfx_metal_set_scene_active(bool active) {
    bool changed = metal_scene_active != active;
    metal_scene_active = active;
    if (changed) {
        gfx_ios_diag_log("Metal scene render state=%s appState=%ld scene=%@ sceneState=%ld", active ? "active" : "inactive", (long)UIApplication.sharedApplication.applicationState, metal_view.window.windowScene.session.persistentIdentifier, (long)metal_view.window.windowScene.activationState);
    }
    if (active && metal_view) {
        gfx_metal_on_resize();
    }
}

static bool gfx_metal_scene_can_render(void) {
    UIWindowScene *scene = metal_view.window.windowScene;
    return metal_scene_active && UIApplication.sharedApplication.applicationState == UIApplicationStateActive &&
           scene && scene.activationState == UISceneActivationStateForegroundActive;
}

bool gfx_metal_attach_to_view_controller(UIViewController *view_controller) {
    if (!view_controller) {
        gfx_ios_diag_log("Metal view attachment failed: SDL UIKit view controller is nil");
        return false;
    }
    if (metal_view) {
        gfx_ios_diag_log("Metal view attachment skipped: already attached view=%@ window=%@", metal_view, metal_view.window);
        [view_controller.view setNeedsLayout];
        [view_controller.view layoutIfNeeded];
        [metal_view setNeedsLayout];
        [metal_view layoutIfNeeded];
        return gfx_metal_surface_is_ready();
    }
    gfx_ios_diag_log("Metal renderer selected; attaching MTKView to SDL controller=%s frame=(%.0f,%.0f %.0fx%.0f) window=%@ scene=%@", NSStringFromClass(view_controller.class).UTF8String ?: "unknown", view_controller.view.frame.origin.x, view_controller.view.frame.origin.y, view_controller.view.frame.size.width, view_controller.view.frame.size.height, view_controller.view.window, view_controller.view.window.windowScene.session.persistentIdentifier);
    metal_device = MTLCreateSystemDefaultDevice();
    if (!metal_device) {
        gfx_ios_diag_log("Metal device initialization failed: MTLCreateSystemDefaultDevice returned nil");
        NSLog(@"Metal initialization failed: no system Metal device is available");
        return false;
    }
    // A macOS-only GPU power-class property caused an unrecognized selector
    // on AGXG15Device. That diagnostic did not affect GPU selection.
    gfx_ios_diag_log("Metal device initialized: %s", metal_device.name.UTF8String ?: "unknown");
    metal_queue = [metal_device newCommandQueue];
    if (!metal_queue) {
        gfx_ios_diag_log("Metal command queue initialization failed for device=%s", metal_device.name.UTF8String ?: "unknown");
        NSLog(@"Metal initialization failed: could not create a command queue on %@", metal_device.name);
        return false;
    }
    gfx_ios_diag_log("Metal command queue initialized");
    metal_view = [[MTKView alloc] initWithFrame:view_controller.view.bounds device:metal_device];
    if (!metal_view) {
        gfx_ios_diag_log("Metal MTKView initialization failed");
        return false;
    }
    metal_view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    metal_view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    metal_view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    // This renderer schedules presentation directly on each command buffer.
    // Transaction synchronized presentation requires a different scheduling path.
    metal_view.presentsWithTransaction = NO;
    metal_view.framebufferOnly = YES;
    metal_view.paused = YES;
    metal_view.enableSetNeedsDisplay = NO;
    metal_view.opaque = YES;
    metal_view.contentScaleFactor = UIScreen.mainScreen.scale;
    metal_view.backgroundColor = UIColor.blackColor;
    // SDL's UIKit view can cover subviews inserted at index zero. Put Metal
    // above it; UIKit touch controls are added afterward and remain on top.
    [view_controller.view addSubview:metal_view];
    [view_controller.view setNeedsLayout];
    [view_controller.view layoutIfNeeded];
    [metal_view setNeedsLayout];
    [metal_view layoutIfNeeded];
    CGSize drawable = metal_view.drawableSize;
    UIWindowScene *scene = metal_view.window.windowScene;
    metal_scene_active = scene && scene.activationState == UISceneActivationStateForegroundActive &&
                         UIApplication.sharedApplication.applicationState == UIApplicationStateActive;
    gfx_ios_diag_log("Game Metal MTKView attached to SDL scene window frame=(%.0f,%.0f %.0fx%.0f) drawable=%.0fx%.0f scale=%.2f color=BGRA8Unorm depth=Depth32Float stencil=None parent=%@ window=%@ scene=%@ hidden=%d alpha=%.2f interaction=%d parentSubviewCount=%lu", metal_view.frame.origin.x, metal_view.frame.origin.y, metal_view.frame.size.width, metal_view.frame.size.height, drawable.width, drawable.height, metal_view.contentScaleFactor, metal_view.superview, metal_view.window, metal_view.window.windowScene.session.persistentIdentifier, metal_view.hidden, metal_view.alpha, metal_view.userInteractionEnabled, (unsigned long)view_controller.view.subviews.count);
    CAMetalLayer *metal_layer = (CAMetalLayer *)metal_view.layer;
    gfx_ios_diag_log("Metal CoreAnimation layer configuration presentsWithTransaction=%d framebufferOnly=%d paused=%d enableSetNeedsDisplay=%d", metal_layer.presentsWithTransaction, metal_view.framebufferOnly, metal_view.paused, metal_view.enableSetNeedsDisplay);
    gfx_ios_diag_log("Metal scene geometry attached sceneBounds=(%.0fx%.0f) windowBounds=(%.0fx%.0f) rootBounds=(%.0fx%.0f) MTKViewBounds=(%.0fx%.0f) drawable=(%.0fx%.0f) scale=%.2f", scene.coordinateSpace.bounds.size.width, scene.coordinateSpace.bounds.size.height, metal_view.window.bounds.size.width, metal_view.window.bounds.size.height, view_controller.view.bounds.size.width, view_controller.view.bounds.size.height, metal_view.bounds.size.width, metal_view.bounds.size.height, drawable.width, drawable.height, metal_view.contentScaleFactor);
    gfx_metal_restore_shader64_pack();
    bool ready = gfx_metal_surface_is_ready();
    gfx_ios_diag_log("Game Metal surface readiness=%s drawable=%.0fx%.0f attachedToScene=%d", ready ? "ready" : "not-ready", drawable.width, drawable.height, metal_view.window.windowScene != nil);
    return ready;
}

static struct MetalProgram *find_program(uint32_t shader_id) {
    for (size_t i = 0; i < program_count; ++i)
        if (programs[i].shader_id == shader_id &&
            programs[i].material_generation == metal_material_generation) return &programs[i];
    return NULL;
}

static void gfx_metal_release_program(struct MetalProgram *program) {
    if (!program) return;
    for (size_t i = 0; i < METAL_PIPELINE_STATE_CAP; ++i) {
        [program->pipelines[i] release];
        program->pipelines[i] = nil;
    }
    [program->fragment release]; program->fragment = nil;
    [program->vertex release]; program->vertex = nil;
    [program->library release]; program->library = nil;
    program->shader_id = UINT32_MAX;
}

static void gfx_metal_unload_shader(struct ShaderProgram *program) {
    (void)program;
    active_program = NULL;
}

static void gfx_metal_load_shader(struct ShaderProgram *program) {
    active_program = (struct MetalProgram *)program;
}

static void gfx_metal_apply_sampler_parameters(int sampler, bool linear,
                                               uint32_t cms, uint32_t cmt);

static struct MetalProgram *metal_program_for(uint32_t shader_id);

static struct ShaderProgram *gfx_metal_create_and_load_new_shader(uint32_t shader_id) {
    struct MetalProgram *p = metal_program_for(shader_id);
    active_program = p;
    return (struct ShaderProgram *)p;
}

static struct ShaderProgram *gfx_metal_lookup_shader(uint32_t shader_id) {
    return (struct ShaderProgram *)find_program(shader_id);
}

static void gfx_metal_shader_get_info(struct ShaderProgram *program, uint8_t *num_inputs, bool used_textures[2]) {
    struct MetalProgram *p = (struct MetalProgram *)program;
    if (num_inputs) *num_inputs = p ? (uint8_t)p->features.num_inputs : 0;
    if (used_textures) {
        used_textures[0] = p && p->features.used_textures[0];
        used_textures[1] = p && p->features.used_textures[1];
    }
}

static uint32_t gfx_metal_new_texture(void) {
    if (texture_count >= METAL_TEXTURE_CAP) {
        gfx_ios_diag_log("Metal texture allocation failed: texture table limit (%d) reached", METAL_TEXTURE_CAP);
        NSLog(@"Metal texture allocation failed: texture table limit (%d) reached", METAL_TEXTURE_CAP);
        return 0;
    }
    return (uint32_t)++texture_count;
}

static void gfx_metal_select_texture(int tile, uint32_t texture_id) {
    if (tile < 0 || tile > 1 || texture_id == 0 || texture_id > texture_count) return;
    current_texture = &textures[texture_id - 1];
    bound_textures[tile] = current_texture;
    /* gfx_pc caches sampler settings with each texture node, while Metal
     * sampler objects belong to tile slots. A texture-cache hit can therefore
     * skip set_sampler_parameters even though another texture changed this
     * slot in the meantime. Restore the selected texture's state here. */
    if (current_texture->sampler_state_valid) {
        struct GfxMetalSamplerState current = {
            sampler_linear[tile], sampler_cms[tile], sampler_cmt[tile]
        };
        struct GfxMetalSamplerState desired = current_texture->sampler_state;
        if (!gfx_metal_sampler_state_matches(sampler_state_valid[tile], current, desired)) {
            uint64_t restore = ++sampler_restore_count;
            if (gfx_metal_diagnostics_enabled() && (restore <= 16 || restore % 128 == 0)) {
                gfx_ios_diag_log("Metal sampler restored on texture bind tile=%d logicalId=%lu previousValid=%d previous=(linear=%d,cms=0x%x,cmt=0x%x) textureState=(linear=%d,cms=0x%x,cmt=0x%x) restoreCount=%llu",
                                 tile, (unsigned long)texture_id,
                                 sampler_state_valid[tile], sampler_linear[tile],
                                 sampler_cms[tile], sampler_cmt[tile],
                                 desired.linear, desired.cms, desired.cmt,
                                 (unsigned long long)restore);
            }
            gfx_metal_apply_sampler_parameters(tile, desired.linear,
                                               desired.cms, desired.cmt);
        }
    }
}

static void gfx_metal_upload_texture(const uint8_t *rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
        gfx_ios_diag_log("Metal texture upload rejected rgba=%p size=%dx%d", rgba, width, height);
        return;
    }
    struct MetalTexture *t = current_texture;
    if (!t) return;
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                   width:(NSUInteger)width
                                                                                  height:(NSUInteger)height
                                                                               mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    [t->texture release];
    t->texture = [metal_device newTextureWithDescriptor:desc];
    if (!t->texture) {
        gfx_ios_diag_log("Metal texture creation failed id=%lu size=%dx%d", (unsigned long)(t - textures + 1), width, height);
        NSLog(@"Metal texture creation failed for id=%lu size=%dx%d", (unsigned long)(t - textures + 1), width, height);
        return;
    }
    [t->texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
                    withBytes:rgba bytesPerRow:(NSUInteger)width * 4];
    t->width = width;
    t->height = height;
    ++t->generation;
    if (gfx_metal_diagnostics_enabled()) {
        uint64_t snapshot_id = ++texture_upload_snapshot_count;
        if (snapshot_id <= 16 || snapshot_id % 128 == 0) {
            size_t pixel_count = (size_t)width * (size_t)height;
            size_t sample_count = MIN(pixel_count, (size_t)4);
            uint8_t pixels[4][4] = {{0}};
            for (size_t i = 0; i < sample_count; ++i)
                memcpy(pixels[i], rgba + i * 4, 4);
            gfx_ios_diag_log("Metal texture upload sample sequence=%llu logicalId=%lu texture=%p generation=%u size=%dx%d pixelFormat=RGBA8Unorm sourceRowPitch=%lu samplePixels=%lu rgba0=(%u,%u,%u,%u) rgba1=(%u,%u,%u,%u) rgba2=(%u,%u,%u,%u) rgba3=(%u,%u,%u,%u)",
                             (unsigned long long)snapshot_id,
                             (unsigned long)(t - textures + 1), t->texture,
                             t->generation, width, height,
                             (unsigned long)((size_t)width * 4),
                             (unsigned long)sample_count,
                             pixels[0][0],pixels[0][1],pixels[0][2],pixels[0][3],
                             pixels[1][0],pixels[1][1],pixels[1][2],pixels[1][3],
                             pixels[2][0],pixels[2][1],pixels[2][2],pixels[2][3],
                             pixels[3][0],pixels[3][1],pixels[3][2],pixels[3][3]);
        }
    }
    if (texture_count <= 3 || texture_count % 128 == 0)
        gfx_ios_diag_log("Metal texture uploaded id=%lu size=%dx%d pixelFormat=RGBA8Unorm totalTextures=%lu", (unsigned long)(t - textures + 1), width, height, (unsigned long)texture_count);
}

static void gfx_metal_apply_sampler_parameters(int sampler, bool linear,
                                               uint32_t cms, uint32_t cmt) {
    if (sampler < 0 || sampler > 1 || !metal_device) return;
    struct GfxMetalSamplerState current = {
        sampler_linear[sampler], sampler_cms[sampler], sampler_cmt[sampler]
    };
    struct GfxMetalSamplerState desired = { linear, cms, cmt };
    NSUInteger requested_anisotropy = linear ? MAX(1u, metal_settings.anisotropy) : 1u;
    if (gfx_metal_sampler_state_matches(sampler_state_valid[sampler], current, desired) &&
        sampler_anisotropy[sampler] == requested_anisotropy) return;

    MTLSamplerDescriptor *desc = [[[MTLSamplerDescriptor alloc] init] autorelease];
    desc.minFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    desc.magFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    enum GfxSamplerAddressMode s_mode = gfx_metal_sampler_address_mode(cms);
    enum GfxSamplerAddressMode t_mode = gfx_metal_sampler_address_mode(cmt);
    desc.sAddressMode = s_mode == GFX_SAMPLER_CLAMP ? MTLSamplerAddressModeClampToEdge :
                        (s_mode == GFX_SAMPLER_MIRROR ? MTLSamplerAddressModeMirrorRepeat : MTLSamplerAddressModeRepeat);
    desc.tAddressMode = t_mode == GFX_SAMPLER_CLAMP ? MTLSamplerAddressModeClampToEdge :
                        (t_mode == GFX_SAMPLER_MIRROR ? MTLSamplerAddressModeMirrorRepeat : MTLSamplerAddressModeRepeat);
    static const NSUInteger anisotropy_levels[] = { 16, 8, 4, 2, 1 };
    id<MTLSamplerState> new_sampler = nil;
    NSUInteger selected_anisotropy = 1;
    for (NSUInteger i = 0; i < sizeof(anisotropy_levels) / sizeof(anisotropy_levels[0]); ++i) {
        NSUInteger level = anisotropy_levels[i];
        if (level > requested_anisotropy) continue;
        desc.maxAnisotropy = level;
        new_sampler = [metal_device newSamplerStateWithDescriptor:desc];
        if (new_sampler) {
            selected_anisotropy = level;
            break;
        }
    }
    if (!new_sampler) {
        NSLog(@"Metal sampler creation failed for tile=%d linear=%d address=(%u,%u)", sampler, linear, cms, cmt);
        return;
    }
    if (linear && selected_anisotropy < requested_anisotropy) {
        gfx_ios_diag_log("Metal anisotropic sampler fell back requested=%lu active=%lu tile=%d", (unsigned long)requested_anisotropy, (unsigned long)selected_anisotropy, sampler);
        metal_capabilities.max_anisotropy = (uint32_t)selected_anisotropy;
        metal_settings.anisotropy = (uint32_t)selected_anisotropy;
        metal_performance_stats.anisotropy = (uint32_t)selected_anisotropy;
        gfx_metal_persist_settings(metal_settings);
    }
    [metal_samplers[sampler] release];
    metal_samplers[sampler] = new_sampler;
    sampler_anisotropy[sampler] = selected_anisotropy;
    sampler_linear[sampler] = linear;
    sampler_cms[sampler] = cms;
    sampler_cmt[sampler] = cmt;
    sampler_state_valid[sampler] = true;
}

static void gfx_metal_set_sampler_parameters(int sampler, bool linear, uint32_t cms, uint32_t cmt) {
    if (sampler < 0 || sampler > 1 || !metal_device) return;
    if (bound_textures[sampler]) {
        bound_textures[sampler]->sampler_state.linear = linear;
        bound_textures[sampler]->sampler_state.cms = cms;
        bound_textures[sampler]->sampler_state.cmt = cmt;
        bound_textures[sampler]->sampler_state_valid = true;
    }
    gfx_metal_apply_sampler_parameters(sampler, linear, cms, cmt);
}

static void gfx_metal_set_depth_test(bool enabled) { state_depth_test = enabled; }
static void gfx_metal_set_depth_mask(bool enabled) { state_depth_write = enabled; }
static void gfx_metal_set_zmode_decal(bool enabled) { state_decal = enabled; }
static void gfx_metal_set_viewport(int x,int y,int w,int h) { viewport[0]=x;viewport[1]=y;viewport[2]=w;viewport[3]=h;has_viewport=true; }
static void gfx_metal_set_scissor(int x,int y,int w,int h) { scissor[0]=x;scissor[1]=y;scissor[2]=w;scissor[3]=h;has_scissor=true; }
static void gfx_metal_set_use_alpha(bool enabled) { state_blend = enabled; }

static struct MetalProgram *metal_program_for(uint32_t shader_id) {
    struct MetalProgram *p = NULL;
    bool append_slot = false;
    char *material_source = NULL;
    size_t material_size = 0;
    bool material_pack_textures = false;
    for (size_t i = 0; i < program_count; ++i)
        if (programs[i].material_generation != metal_material_generation)
            gfx_metal_release_program(&programs[i]);
    p = find_program(shader_id);
    if (p) return p;
    for (size_t i = 0; i < program_count; ++i) {
        if (programs[i].shader_id == UINT32_MAX) { p = &programs[i]; break; }
    }
    if (!p && program_count < METAL_SHADER_CAP) {
        p = &programs[program_count];
        append_slot = true;
    }
    if (!p) return NULL;
    @synchronized ([NSObject class]) {
        struct MetalShader64Graph *graph = metal_shader64_graph;
        if (graph && graph->material_source && graph->material_source_size) {
            material_source = malloc(graph->material_source_size);
            if (material_source) {
                memcpy(material_source, graph->material_source, graph->material_source_size);
                material_size = graph->material_source_size;
                material_pack_textures = graph->material_pack_textures;
            }
        }
    }
    size_t source_capacity = 8192u + material_size + 1u;
    char *source = malloc(source_capacity);
    if (!source) { free(material_source); return NULL; }
    struct CCFeatures features;
    NSError *error = nil;
    bool generated = material_source ? gfx_metal_build_shader_with_material(
        shader_id, material_source, material_size, material_pack_textures, source, source_capacity, &features) :
        gfx_metal_build_shader(shader_id, source, source_capacity, &features);
    if (!generated && material_source) {
        gfx_ios_diag_log("SHADER64 material hook rejected by generated shader shader=0x%08x; using Fast3D combiner", shader_id);
        free(material_source); material_source = NULL; material_size = 0;
        generated = gfx_metal_build_shader(shader_id, source, source_capacity, &features);
    }
    if (!generated) {
        free(source); free(material_source);
        gfx_ios_diag_log("Metal shader generation failed shader=0x%08x", shader_id);
        NSLog(@"Metal shader generation failed for shader=0x%08x", shader_id);
        return NULL;
    }
    NSString *msl = [NSString stringWithUTF8String:source];
    id<MTLLibrary> library = [metal_device newLibraryWithSource:msl options:nil error:&error];
    free(source);
    if (!library && material_source) {
        gfx_ios_diag_log("SHADER64 material hook compile failed shader=0x%08x; using Fast3D combiner error=%s",
                         shader_id, error.localizedDescription.UTF8String ?: "unknown");
        char fallback[8192];
        error = nil;
        if (gfx_metal_build_shader(shader_id, fallback, sizeof(fallback), &features)) {
            NSString *fallback_msl = [NSString stringWithUTF8String:fallback];
            library = [metal_device newLibraryWithSource:fallback_msl options:nil error:&error];
        }
    }
    free(material_source);
    if (!library) {
        gfx_ios_diag_log("Metal MSL compilation failed shader=0x%08x inputs=%d textures=(%d,%d) fog=%d alpha=%d error=%s", shader_id, features.num_inputs, features.used_textures[0], features.used_textures[1], features.opt_fog, features.opt_alpha, error.localizedDescription.UTF8String ?: "unknown");
        NSLog(@"Metal shader compilation failed shader=0x%08x: %@", shader_id, error.localizedDescription);
        return NULL;
    }
    id<MTLFunction> vertex = [library newFunctionWithName:@"sm64_vertex"];
    id<MTLFunction> fragment = [library newFunctionWithName:@"sm64_fragment"];
    if (!vertex || !fragment) {
        gfx_ios_diag_log("Metal shader stage lookup failed shader=0x%08x vertex=%s fragment=%s", shader_id, vertex ? "found" : "missing", fragment ? "found" : "missing");
        NSLog(@"Generated Metal shader is missing a stage function shader=0x%08x", shader_id);
        [vertex release]; [fragment release]; [library release];
        return NULL;
    }
    memset(p, 0, sizeof(*p));
    p->shader_id = shader_id;
    p->material_generation = metal_material_generation;
    if (append_slot) ++program_count;
    p->library = library;
    p->vertex = vertex;
    p->fragment = fragment;
    p->features = features;
    gfx_ios_diag_log("Metal MSL shader ready shader=0x%08x inputs=%d textures=(%d,%d) fog=%d alpha=%d programCount=%lu", shader_id, features.num_inputs, features.used_textures[0], features.used_textures[1], features.opt_fog, features.opt_alpha, (unsigned long)program_count);
    return p;
}

static id<MTLRenderPipelineState> pipeline_for(struct MetalProgram *p) {
    uint32_t state_key = gfx_metal_pipeline_state_key(state_depth_test, state_depth_write,
                                                       state_blend, state_decal);
    uint32_t key = gfx_metal_pipeline_cache_index((uint32_t)metal_active_sample_count, state_key);
    if (key != UINT32_MAX && metal_frame_uses_offscreen) key += 48u;
    if (key == UINT32_MAX || key >= METAL_PIPELINE_STATE_CAP) return nil;
    if (p->pipelines[key]) return p->pipelines[key];
    struct GfxMetalVertexLayout vertex_layout = {0};
    if (!gfx_metal_get_vertex_layout(&p->features, &vertex_layout)) {
        gfx_ios_diag_log("Metal vertex layout rejected shader=0x%08x inputs=%d", p->shader_id, p->features.num_inputs);
        return nil;
    }
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    for (NSUInteger attr = 0; attr < vertex_layout.attribute_count; ++attr) {
        switch (vertex_layout.format[attr]) {
            case GFX_METAL_VERTEX_FLOAT2: vd.attributes[attr].format = MTLVertexFormatFloat2; break;
            case GFX_METAL_VERTEX_FLOAT3: vd.attributes[attr].format = MTLVertexFormatFloat3; break;
            case GFX_METAL_VERTEX_FLOAT4: vd.attributes[attr].format = MTLVertexFormatFloat4; break;
            default:
                gfx_ios_diag_log("Metal vertex layout has invalid format shader=0x%08x attribute=%lu format=%u", p->shader_id, (unsigned long)attr, vertex_layout.format[attr]);
                return nil;
        }
        vd.attributes[attr].offset = vertex_layout.offset[attr];
        vd.attributes[attr].bufferIndex = 0;
    }
    vd.layouts[0].stride = vertex_layout.stride;
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    MTLRenderPipelineDescriptor *desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    desc.vertexFunction = p->vertex; desc.fragmentFunction = p->fragment;
    desc.vertexDescriptor = vd;
    desc.colorAttachments[0].pixelFormat = metal_frame_uses_offscreen ?
        MTLPixelFormatRGBA16Float : metal_view.colorPixelFormat;
    desc.depthAttachmentPixelFormat = metal_view.depthStencilPixelFormat;
    desc.sampleCount = metal_active_sample_count;
    if (state_blend) {
        MTLRenderPipelineColorAttachmentDescriptor *color = desc.colorAttachments[0];
        color.blendingEnabled = YES;
        color.rgbBlendOperation = MTLBlendOperationAdd;
        color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError *error = nil;
    p->pipelines[key] = [metal_device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!p->pipelines[key]) {
        if (logged_pipeline_failures++ < 16)
            gfx_ios_diag_log("Metal render pipeline creation failed shader=0x%08x state=0x%x samples=%lu colorFormat=%lu depthFormat=%lu error=%s", p->shader_id, state_key, (unsigned long)metal_active_sample_count, (unsigned long)metal_view.colorPixelFormat, (unsigned long)metal_view.depthStencilPixelFormat, error.localizedDescription.UTF8String ?: "unknown");
        NSLog(@"Metal pipeline creation failed shader=0x%08x state=0x%x samples=%lu: %@",
              p->shader_id, state_key, (unsigned long)metal_active_sample_count, error.localizedDescription);
    } else if (logged_pipeline_successes++ < 16) {
        gfx_ios_diag_log("Metal pipeline created shader=0x%08x state=0x%x samples=%lu", p->shader_id, state_key, (unsigned long)metal_active_sample_count);
    }
    return p->pipelines[key];
}

static int gfx_metal_batch_has_portal_color(const float *data, size_t float_count,
                                            size_t triangle_count,
                                            const struct GfxMetalVertexLayout *layout,
                                            const struct CCFeatures *features,
                                            int portalType) {
    float expected[3];
    size_t vertexCount = triangle_count * 3u;
    size_t vertex, base;
    uint32_t components = features->opt_alpha ? 4u : 3u;
    if (features->num_inputs < 1 || !layout->attribute_count) return 0;
    expected[0] = portalType == 0 ? 40.0f / 255.0f : 1.0f;
    expected[1] = portalType == 0 ? 170.0f / 255.0f : 120.0f / 255.0f;
    expected[2] = portalType == 0 ? 1.0f : 24.0f / 255.0f;
    for (vertex = 0; vertex + 2 < vertexCount; vertex += 3) {
        int allMatch = 1;
        int k;
        for (k = 0; k < 3; ++k) {
            base = vertex * (layout->stride / sizeof(float)) + k * (layout->stride / sizeof(float))
                 + layout->offset[layout->input_attribute[0]] / sizeof(float);
            if (base + components > float_count
                || fabsf(data[base] - expected[0]) > 0.004f
                || fabsf(data[base + 1] - expected[1]) > 0.004f
                || fabsf(data[base + 2] - expected[2]) > 0.004f) {
                allMatch = 0;
                break;
            }
        }
        if (allMatch) return 1;
    }
    return 0;
}

static void gfx_metal_draw_triangles(float data[], size_t float_count, size_t triangle_count) {
    if (!triangle_count) return;
    if (!gfx_metal_scene_can_render()) return;
    if (!metal_encoder || !active_program || !data) {
        if (logged_draw_warnings++ < 12)
            gfx_ios_diag_log("Metal triangle draw skipped encoder=%s shader=%s data=%s triangles=%lu", metal_encoder ? "ready" : "nil", active_program ? "selected" : "nil", data ? "ready" : "nil", (unsigned long)triangle_count);
        return;
    }
    struct GfxMetalVertexLayout vertex_layout = {0};
    if (!gfx_metal_get_vertex_layout(&active_program->features, &vertex_layout) ||
        !gfx_metal_vertex_buffer_size_matches(&vertex_layout, float_count, triangle_count)) {
        if (logged_draw_warnings++ < 12)
            gfx_ios_diag_log("Metal triangle draw skipped: packed vertex buffer does not match shader layout shader=0x%08x floats=%lu triangles=%lu stride=%u", active_program->shader_id, (unsigned long)float_count, (unsigned long)triangle_count, vertex_layout.stride);
        return;
    }
    id<MTLRenderPipelineState> pipeline = pipeline_for(active_program);
    if (!pipeline) {
        if (metal_frame_uses_offscreen && metal_active_sample_count > 1) {
            metal_offscreen_runtime_failed = true;
            gfx_ios_diag_log("Metal enhanced pipeline failed shader=0x%08x samples=%lu; next frame will retry on direct Classic-compatible attachments", active_program->shader_id, (unsigned long)metal_active_sample_count);
        }
        if (logged_draw_warnings++ < 12)
            gfx_ios_diag_log("Metal triangle draw skipped: no render pipeline shader=0x%08x triangles=%lu", active_program->shader_id, (unsigned long)triangle_count);
        return;
    }
    id<MTLBuffer> buffer = [metal_device newBufferWithBytes:data length:float_count * sizeof(float) options:MTLResourceStorageModeShared];
    if (!buffer) {
        if (logged_draw_warnings++ < 12) gfx_ios_diag_log("Metal vertex buffer allocation failed shader=0x%08x bytes=%lu", active_program->shader_id, (unsigned long)(float_count * sizeof(float)));
        return;
    }
    [metal_encoder setRenderPipelineState:pipeline];
    [metal_encoder setVertexBuffer:buffer offset:0 atIndex:0];
    if (active_program->features.used_textures[0]) {
        if (!bound_textures[0] || !bound_textures[0]->texture || !metal_samplers[0]) {
            if (logged_draw_warnings++ < 12) gfx_ios_diag_log("Metal draw missing texture/sampler shader=0x%08x tile=0 texture=%s sampler=%s", active_program->shader_id, bound_textures[0] && bound_textures[0]->texture ? "ready" : "nil", metal_samplers[0] ? "ready" : "nil");
            [buffer release]; return;
        }
        [metal_encoder setFragmentTexture:bound_textures[0]->texture atIndex:0];
        [metal_encoder setFragmentSamplerState:metal_samplers[0] atIndex:0];
    }
    if (active_program->features.used_textures[1]) {
        if (!bound_textures[1] || !bound_textures[1]->texture || !metal_samplers[1]) {
            if (logged_draw_warnings++ < 12) gfx_ios_diag_log("Metal draw missing texture/sampler shader=0x%08x tile=1 texture=%s sampler=%s", active_program->shader_id, bound_textures[1] && bound_textures[1]->texture ? "ready" : "nil", metal_samplers[1] ? "ready" : "nil");
            [buffer release]; return;
        }
        [metal_encoder setFragmentTexture:bound_textures[1]->texture atIndex:1];
        [metal_encoder setFragmentSamplerState:metal_samplers[1] atIndex:1];
    }
    struct MetalFragmentParams fragment_params = {
        configFiltering == 2 && sampler_linear[0] ? 1u : 0u,
        configFiltering == 2 && sampler_linear[1] ? 1u : 0u,
        (uint32_t)gfx_ios_render_diagnostic_mode(),
        bound_textures[0] ? (uint32_t)(bound_textures[0] - textures + 1) : 0u,
        bound_textures[1] ? (uint32_t)(bound_textures[1] - textures + 1) : 0u,
        gfx_current_render_material_class(),
        (float)frame_count
    };
    [metal_encoder setFragmentBytes:&fragment_params length:sizeof(fragment_params) atIndex:1];
    float material_parameters[4] = {0};
    bool has_material_hook = false;
    bool material_uses_pack_textures = false;
    uint32_t material_pack_texture_count = 0;
    id<MTLTexture> material_pack_textures[GFX_SHADER64_MAX_PACK_TEXTURES] = { nil };
    @synchronized ([NSObject class]) {
        if (metal_shader64_graph && metal_shader64_graph->material_source &&
            metal_shader64_graph->material_source_size) {
            material_parameters[0] = metal_shader64_parameters.x;
            material_parameters[1] = metal_shader64_parameters.y;
            material_parameters[2] = metal_shader64_parameters.z;
            material_parameters[3] = metal_shader64_parameters.w;
            has_material_hook = true;
            material_uses_pack_textures = metal_shader64_graph->material_pack_textures;
            if (material_uses_pack_textures) {
                material_pack_texture_count = (uint32_t)metal_shader64_graph->pack_texture_count;
                for (NSUInteger i = 0; i < GFX_SHADER64_MAX_PACK_TEXTURES; ++i) {
                    NSUInteger slot = MIN(i, metal_shader64_graph->pack_texture_count - 1u);
                    material_pack_textures[i] = [metal_shader64_graph->pack_textures[slot] retain];
                }
            }
        }
    }
    if (has_material_hook)
        [metal_encoder setFragmentBytes:material_parameters length:sizeof(material_parameters) atIndex:2];
    if (material_uses_pack_textures && material_pack_texture_count > 0u) {
        for (NSUInteger i = 0; i < GFX_SHADER64_MAX_PACK_TEXTURES; ++i) {
            [metal_encoder setFragmentTexture:material_pack_textures[i] atIndex:4u + i];
            [metal_encoder setFragmentSamplerState:metal_present_sampler atIndex:4u + i];
            [material_pack_textures[i] release];
        }
        [metal_encoder setFragmentBytes:&material_pack_texture_count
                                length:sizeof(material_pack_texture_count) atIndex:3];
    }

    uint64_t batch_id = ++metal_draw_batch_count;
    if (gfx_metal_diagnostics_enabled() && (batch_id <= 12 || batch_id % 256 == 0)) {
        const float *v0 = data;
        const float *v1 = (const float *)((const uint8_t *)data + vertex_layout.stride);
        const float *v2 = (const float *)((const uint8_t *)data + 2u * vertex_layout.stride);
        float uv0[2] = {0.0f, 0.0f}, uv1[2] = {0.0f, 0.0f}, uv2[2] = {0.0f, 0.0f};
        if (vertex_layout.has_uv) {
            const float *uv_offset = (const float *)((const uint8_t *)data + vertex_layout.offset[vertex_layout.uv_attribute]);
            uv0[0] = uv_offset[0]; uv0[1] = uv_offset[1];
            uv_offset = (const float *)((const uint8_t *)data + vertex_layout.stride + vertex_layout.offset[vertex_layout.uv_attribute]);
            uv1[0] = uv_offset[0]; uv1[1] = uv_offset[1];
            uv_offset = (const float *)((const uint8_t *)data + 2u * vertex_layout.stride + vertex_layout.offset[vertex_layout.uv_attribute]);
            uv2[0] = uv_offset[0]; uv2[1] = uv_offset[1];
        }
        gfx_ios_diag_log("Metal render snapshot frame=%u drawBatch=%llu shader=0x%08x pipeline=%p mode=%u triangles=%lu vertexBuffer=%p offset=0 bytes=%lu capacity=%lu stride=%u attributes=%u inputs=%d hasUV=%d uvAttr=%u hasFog=%d fogAttr=%u vertex0=(%.4f,%.4f,%.4f,%.4f) vertex1=(%.4f,%.4f,%.4f,%.4f) vertex2=(%.4f,%.4f,%.4f,%.4f) uv0=(%.4f,%.4f) uv1=(%.4f,%.4f) uv2=(%.4f,%.4f) depthTest=%d depthWrite=%d blend=%d decal=%d",
                         frame_count, (unsigned long long)batch_id, active_program->shader_id, pipeline,
                         fragment_params.diagnostic_mode, (unsigned long)triangle_count, buffer,
                         (unsigned long)(float_count * sizeof(float)), (unsigned long)buffer.length,
                         vertex_layout.stride, vertex_layout.attribute_count,
                         active_program->features.num_inputs, vertex_layout.has_uv,
                         vertex_layout.uv_attribute, vertex_layout.has_fog,
                         vertex_layout.fog_attribute,
                         v0[0],v0[1],v0[2],v0[3],v1[0],v1[1],v1[2],v1[3],v2[0],v2[1],v2[2],v2[3],
                         uv0[0],uv0[1],uv1[0],uv1[1],uv2[0],uv2[1],
                         state_depth_test, state_depth_write, state_blend, state_decal);
        for (NSUInteger attribute = 0; attribute < vertex_layout.attribute_count; ++attribute) {
            gfx_ios_diag_log("Metal render attribute frame=%u drawBatch=%llu index=%lu format=%u offset=%u stride=%u bufferIndex=0",
                             frame_count, (unsigned long long)batch_id,
                             (unsigned long)attribute, vertex_layout.format[attribute],
                             vertex_layout.offset[attribute], vertex_layout.stride);
        }
        for (int tile = 0; tile < 2; ++tile) {
            struct MetalTexture *t = bound_textures[tile];
            if (active_program->features.used_textures[tile]) {
                gfx_ios_diag_log("Metal render texture frame=%u drawBatch=%llu tile=%d logicalId=%lu texture=%p size=%dx%d format=%lu generation=%u sampler=%p linear=%d addressMode=(0x%x,0x%x)",
                                 frame_count, (unsigned long long)batch_id, tile,
                                 t ? (unsigned long)(t - textures + 1) : 0ul,
                                 t ? t->texture : nil, t ? t->width : 0, t ? t->height : 0,
                                 t && t->texture ? (unsigned long)t->texture.pixelFormat : 0ul,
                                 t ? t->generation : 0, metal_samplers[tile], sampler_linear[tile],
                                 sampler_cms[tile], sampler_cmt[tile]);
            }
        }
    }
    NSUInteger width = metal_active_render_width;
    NSUInteger height = metal_active_render_height;
    /* Keep the tested Classic point-to-pixel transform byte-for-byte in
     * behavior; scaled targets derive independent rounded X/Y ratios. */
    double scale_x = metal_frame_uses_offscreen && metal_view.bounds.size.width > 0.0 ?
        (double)width / metal_view.bounds.size.width : metal_view.contentScaleFactor;
    double scale_y = metal_frame_uses_offscreen && metal_view.bounds.size.height > 0.0 ?
        (double)height / metal_view.bounds.size.height : metal_view.contentScaleFactor;
    MTLViewport vp = {0, 0, (double)width, (double)height, 0.0, 1.0};
    if (has_viewport) {
        double x = viewport[0] * scale_x, y = viewport[1] * scale_y;
        double w = viewport[2] * scale_x, h = viewport[3] * scale_y;
        vp = (MTLViewport){x, height - y - h, w, h, 0.0, 1.0};
    }
    [metal_encoder setViewport:vp];
    NSInteger scissor_x = 0, scissor_y = 0;
    NSInteger scissor_width = (NSInteger)width, scissor_height = (NSInteger)height;
    if (has_scissor) {
        scissor_x = (NSInteger)(scissor[0] * scale_x);
        scissor_y = (NSInteger)(height - (scissor[1] + scissor[3]) * scale_y);
        scissor_width = (NSInteger)(scissor[2] * scale_x);
        scissor_height = (NSInteger)(scissor[3] * scale_y);
        scissor_x = MIN(MAX(scissor_x, 0), (NSInteger)width);
        scissor_y = MIN(MAX(scissor_y, 0), (NSInteger)height);
        scissor_width = MIN(scissor_width, (NSInteger)width - scissor_x);
        scissor_height = MIN(scissor_height, (NSInteger)height - scissor_y);
        if (scissor_width < 0) scissor_width = 0;
        if (scissor_height < 0) scissor_height = 0;
        MTLScissorRect sr = {(NSUInteger)scissor_x, (NSUInteger)scissor_y, (NSUInteger)scissor_width, (NSUInteger)scissor_height};
        [metal_encoder setScissorRect:sr];
    }
    if (log_next_draw_rects) {
        gfx_ios_diag_log("Metal post-resume draw rectangles shader=0x%08x hasViewport=%d logicalViewport=(%d,%d %dx%d) effectiveViewport=(%.1f,%.1f %.1fx%.1f) hasScissor=%d logicalScissor=(%d,%d %dx%d) effectiveScissor=(%ld,%ld %ldx%ld) drawable=%lux%lu render=%lux%lu scale=(%.3f,%.3f)", active_program->shader_id, has_viewport, viewport[0], viewport[1], viewport[2], viewport[3], vp.originX, vp.originY, vp.width, vp.height, has_scissor, scissor[0], scissor[1], scissor[2], scissor[3], (long)scissor_x, (long)scissor_y, (long)scissor_width, (long)scissor_height, (unsigned long)metal_view.drawableSize.width, (unsigned long)metal_view.drawableSize.height, (unsigned long)width, (unsigned long)height, scale_x, scale_y);
        log_next_draw_rects = false;
    }
    int depth_test_key = state_depth_test ? 1 : 0;
    int depth_write_key = state_depth_write ? 1 : 0;
    if (!depth_states[depth_test_key][depth_write_key]) {
        MTLDepthStencilDescriptor *depth_desc = [[[MTLDepthStencilDescriptor alloc] init] autorelease];
        depth_desc.depthCompareFunction = state_depth_test ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
        depth_desc.depthWriteEnabled = state_depth_write;
        depth_states[depth_test_key][depth_write_key] = [metal_device newDepthStencilStateWithDescriptor:depth_desc];
    }
    id<MTLDepthStencilState> depth_state = depth_states[depth_test_key][depth_write_key];
    [metal_encoder setDepthStencilState:depth_state];
    if (state_decal) [metal_encoder setDepthBias:-1.0 slopeScale:0.0 clamp:0.0];
    else [metal_encoder setDepthBias:0.0 slopeScale:0.0 clamp:0.0];
    [metal_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:triangle_count * 3];
    for (int portalType = 0; portalType < 2; ++portalType) {
        if (!logged_portal_draw[portalType]
            && gfx_metal_batch_has_portal_color(data, float_count, triangle_count,
                                                &vertex_layout, &active_program->features,
                                                portalType)) {
            logged_portal_draw[portalType] = true;
            gfx_ios_diag_log("Metal portal geometry draw encoded type=%s frame=%u pipeline=%p shader=0x%08x triangles=%lu colorSignature=(%.3f,%.3f,%.3f) depthTest=%d depthWrite=%d blend=%d drawable=%lux%lu", portalType == 0 ? "blue" : "orange", frame_count, pipeline, active_program->shader_id, (unsigned long)triangle_count, portalType == 0 ? 40.0 / 255.0 : 1.0, portalType == 0 ? 170.0 / 255.0 : 120.0 / 255.0, portalType == 0 ? 1.0 : 24.0 / 255.0, state_depth_test, state_depth_write, state_blend, (unsigned long)metal_active_render_width, (unsigned long)metal_active_render_height);
        }
    }
    ++metal_frame_draw_calls;
    metal_frame_triangles += triangle_count;
    @synchronized ([NSObject class]) {
        metal_performance_stats.draw_calls = metal_frame_draw_calls;
        metal_performance_stats.triangles = metal_frame_triangles;
    }
    if (!logged_first_triangle_draw) {
        logged_first_triangle_draw = true;
        gfx_ios_diag_log("Metal first game triangle draw encoded shader=0x%08x triangles=%lu vertices=%lu renderTarget=%lux%lu samples=%lu", active_program->shader_id, (unsigned long)triangle_count, (unsigned long)(triangle_count * 3), (unsigned long)metal_active_render_width, (unsigned long)metal_active_render_height, (unsigned long)metal_active_sample_count);
    }
    [buffer release];
}

static bool gfx_metal_create_present_pipeline(void) {
    if (metal_present_pipeline && metal_present_sampler) return true;
    NSString *source = [NSString stringWithUTF8String:gfx_metal_present_shader_source()];
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        gfx_ios_diag_log("Metal final-pass MSL compilation failed error=%s", error.localizedDescription.UTF8String ?: "unknown");
        return false;
    }
    id<MTLFunction> vertex = [library newFunctionWithName:@"sm64_present_vertex"];
    id<MTLFunction> fragment = [library newFunctionWithName:@"sm64_present_fragment"];
    if (!vertex || !fragment) {
        gfx_ios_diag_log("Metal final-pass shader functions missing vertex=%d fragment=%d", vertex != nil, fragment != nil);
        [vertex release]; [fragment release]; [library release];
        return false;
    }
    MTLRenderPipelineDescriptor *descriptor = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = metal_view.colorPixelFormat;
    descriptor.sampleCount = 1;
    metal_present_pipeline = [metal_device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    [vertex release]; [fragment release]; [library release];
    if (!metal_present_pipeline) {
        gfx_ios_diag_log("Metal final-pass pipeline creation failed format=%lu error=%s", (unsigned long)metal_view.colorPixelFormat, error.localizedDescription.UTF8String ?: "unknown");
        return false;
    }
    MTLSamplerDescriptor *sampler_descriptor = [[[MTLSamplerDescriptor alloc] init] autorelease];
    sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    metal_present_sampler = [metal_device newSamplerStateWithDescriptor:sampler_descriptor];
    if (!metal_present_sampler) {
        gfx_ios_diag_log("Metal final-pass sampler creation failed");
        [metal_present_pipeline release]; metal_present_pipeline = nil;
        return false;
    }
    gfx_ios_diag_log("Metal final-pass pipeline ready format=BGRA8Unorm sampleCount=1 filter=linear");
    return true;
}

static bool gfx_metal_shader64_valid_id(const char *id) {
    if (!id || !*id || strlen(id) > 64) return false;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return false;
    return true;
}

static void gfx_metal_shader64_graph_release(struct MetalShader64Graph *graph) {
    if (!graph) return;
    bool destroy = false;
    @synchronized ([NSObject class]) {
        if (graph->references && --graph->references == 0) destroy = true;
    }
    if (!destroy) return;
    for (size_t i = 0; i < graph->count; ++i) {
        [graph->passes[i].target release];
        [graph->passes[i].pipeline release];
        [graph->passes[i].compute_pipeline release];
        [graph->passes[i].library release];
    }
    for (NSUInteger i = 0; i < graph->pack_texture_count; ++i)
        [graph->pack_textures[i] release];
    free(graph->material_source);
    free(graph);
}

static id<MTLTexture> gfx_metal_shader64_target(struct MetalShader64GraphPass *pass,
                                                NSUInteger width, NSUInteger height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (pass->target && pass->target_width == width && pass->target_height == height) return pass->target;
    [pass->target release]; pass->target = nil;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                           width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
        (pass->node_type == GFX_SHADER64_NODE_COMPUTE ? MTLTextureUsageShaderWrite : 0u);
    pass->target = [metal_device newTextureWithDescriptor:descriptor];
    if (!pass->target) return nil;
    pass->target.label = [NSString stringWithFormat:@"SHADER64 %s output", pass->id];
    pass->target_width = width; pass->target_height = height;
    return pass->target;
}

static bool gfx_metal_shader64_load_pack_textures(const char *pack_id,
                                                   struct MetalShader64Graph *graph,
                                                   char *error_out, size_t error_size) {
    NSArray<NSString *> *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
        NSUserDomainMask, YES);
    if (!documents.count || !pack_id || !graph) {
        if (error_out && error_size) snprintf(error_out, error_size, "texture storage is unavailable");
        return false;
    }
    NSString *pack_root = [[documents[0] stringByAppendingPathComponent:@"shaderpacks"]
        stringByAppendingPathComponent:[NSString stringWithUTF8String:pack_id]];
    NSData *manifest_data = [NSData dataWithContentsOfFile:[pack_root stringByAppendingPathComponent:@"manifest.json"]];
    NSDictionary *manifest = manifest_data ? [NSJSONSerialization JSONObjectWithData:manifest_data options:0 error:nil] : nil;
    NSArray *definitions = [manifest[@"textures"] isKindOfClass:[NSArray class]] ? manifest[@"textures"] : nil;
    if (!definitions || definitions.count == 0 || definitions.count > GFX_SHADER64_MAX_PACK_TEXTURES) {
        if (error_out && error_size) snprintf(error_out, error_size, "textured pass needs 1 to %u declared PNG textures", GFX_SHADER64_MAX_PACK_TEXTURES);
        return false;
    }
    NSMutableSet<NSString *> *ids = [NSMutableSet set];
    NSMutableSet<NSString *> *paths = [NSMutableSet set];
    NSUInteger total_pixels = 0;
    for (NSUInteger i = 0; i < definitions.count; ++i) {
        NSDictionary *definition = definitions[i];
        NSString *identifier = [definition isKindOfClass:[NSDictionary class]] ? definition[@"id"] : nil;
        NSString *relative = [definition isKindOfClass:[NSDictionary class]] ? definition[@"source"] : nil;
        NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"^[A-Za-z0-9_-]{1,32}$" options:0 error:nil];
        BOOL id_ok = [identifier isKindOfClass:[NSString class]] &&
            [regex numberOfMatchesInString:identifier options:0 range:NSMakeRange(0, identifier.length)] == 1;
        BOOL path_ok = [relative isKindOfClass:[NSString class]] && [relative hasPrefix:@"textures/"] &&
            ![relative containsString:@".."] && relative.pathComponents.count == 2 && [relative hasSuffix:@".png"];
        if (!id_ok || !path_ok || [ids containsObject:identifier] || [paths containsObject:relative]) {
            if (error_out && error_size) snprintf(error_out, error_size, "invalid or duplicate texture id/path at slot %lu", (unsigned long)i);
            return false;
        }
        [ids addObject:identifier]; [paths addObject:relative];
        NSString *path = [pack_root stringByAppendingPathComponent:relative];
        NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
        unsigned long long file_size = [attributes[NSFileSize] unsignedLongLongValue];
        if (![attributes[NSFileType] isEqualToString:NSFileTypeRegular] ||
            file_size == 0 || file_size > GFX_SHADER64_MAX_PACK_TEXTURE_BYTES) {
            if (error_out && error_size) snprintf(error_out, error_size, "texture %lu is missing or exceeds 4 MiB", (unsigned long)i);
            return false;
        }
        UIImage *image = [UIImage imageWithContentsOfFile:path];
        CGImageRef cg_image = image.CGImage;
        size_t width = cg_image ? CGImageGetWidth(cg_image) : 0;
        size_t height = cg_image ? CGImageGetHeight(cg_image) : 0;
        if (!width || !height || width > 2048 || height > 2048 ||
            width > 4194304u / height || total_pixels > 4194304u - width * height) {
            if (error_out && error_size) snprintf(error_out, error_size, "texture dimensions exceed the per-pack 4 megapixel limit");
            return false;
        }
        total_pixels += width * height;
        size_t row_bytes = width * 4u, byte_count = row_bytes * height;
        uint8_t *pixels = calloc(byte_count, 1u);
        CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = pixels && color_space ? CGBitmapContextCreate(pixels, width, height,
            8, row_bytes, color_space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big) : NULL;
        if (!context) {
            free(pixels); if (color_space) CGColorSpaceRelease(color_space);
            if (error_out && error_size) snprintf(error_out, error_size, "could not decode texture %lu to RGBA8", (unsigned long)i);
            return false;
        }
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), cg_image);
        CGContextRelease(context); CGColorSpaceRelease(color_space);
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:width height:height mipmapped:NO];
        descriptor.storageMode = MTLStorageModeShared;
        descriptor.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [metal_device newTextureWithDescriptor:descriptor];
        if (texture) [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
            withBytes:pixels bytesPerRow:row_bytes];
        free(pixels);
        if (!texture) {
            if (error_out && error_size) snprintf(error_out, error_size, "Metal texture allocation failed at slot %lu", (unsigned long)i);
            return false;
        }
        texture.label = [NSString stringWithFormat:@"SHADER64 %@ texture %lu", identifier, (unsigned long)i];
        graph->pack_textures[i] = texture;
        graph->pack_texture_count = i + 1u;
    }
    if (error_out && error_size) error_out[0] = '\0';
    return true;
}

static bool gfx_metal_encode_shader64_graph(id<CAMetalDrawable> drawable,
                                            id<MTLCommandBuffer> command,
                                            id<MTLTexture> scene_source) {
    struct MetalShader64Graph *graph = nil;
    vector_float4 parameters = {1, 0, 0, 0};
    @synchronized ([NSObject class]) {
        graph = metal_shader64_graph;
        if (graph) ++graph->references;
        parameters = metal_shader64_parameters;
    }
    if (!graph) return false;
    bool ok = scene_source != nil;
    double now = CACurrentMediaTime();
    if (shader64_start_time == 0.0) shader64_start_time = now;
    float delta_time = shader64_previous_time > 0.0 ?
        (float)fmin(fmax(now - shader64_previous_time, 0.0), 0.1) : 0.0f;
    shader64_previous_time = now;
    struct MetalShader64FrameInfo frame_info = {
        (float)(now - shader64_start_time), delta_time, (float)frame_count,
        drawable.texture.height ? (float)drawable.texture.width / (float)drawable.texture.height : 1.0f
    };
    bool requires_scene_depth = false;
    bool requires_lighting = false;
    bool requires_shadows = false;
    for (size_t i = 0; i < graph->count; ++i)
        if (graph->passes[i].resource_flags & (GFX_SHADER64_RESOURCE_SCENE_DEPTH |
                                                GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT |
                                                GFX_SHADER64_RESOURCE_SHADOW_MAP |
                                                GFX_SHADER64_RESOURCE_SCENE_NORMALS))
            requires_scene_depth = true;
    for (size_t i = 0; i < graph->count; ++i) {
        if (graph->passes[i].resource_flags & (GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT |
                                                GFX_SHADER64_RESOURCE_SHADOW_MAP))
            requires_lighting = true;
        if (graph->passes[i].resource_flags & GFX_SHADER64_RESOURCE_SHADOW_MAP)
            requires_shadows = true;
    }
    float inverse_projection[4][4] = {{0}};
    float inverse_view[4][4] = {{0}};
    vector_float4 sun_direction_and_intensity = {0, 1, 0, 0};
    struct GfxSceneCaptureFrame capture = gfx_scene_capture_get_frame();
    if (requires_scene_depth) {
        if (!metal_depth_texture || metal_depth_samples != 1 || !capture.has_projection_matrix ||
            !gfx_postprocess_invert_matrix(capture.projection_matrix, inverse_projection)) {
            static uint32_t depth_unavailable_logs;
            if (depth_unavailable_logs++ < 8)
                gfx_ios_diag_log("SHADER64 sceneDepth unavailable this frame depth=%d samples=%lu projection=%d; using native composite",
                    metal_depth_texture != nil, (unsigned long)metal_depth_samples, capture.has_projection_matrix);
            gfx_metal_shader64_graph_release(graph);
            return false;
        }
        if (requires_lighting) {
            if (!capture.has_view_matrix || !gfx_postprocess_invert_matrix(capture.view_matrix, inverse_view)) {
                static uint32_t light_unavailable_logs;
                if (light_unavailable_logs++ < 8)
                    gfx_ios_diag_log("SHADER64 directional light unavailable: inverse view matrix missing; using native composite");
                gfx_metal_shader64_graph_release(graph);
                return false;
            }
            static const float intensities[] = {0.0f, 0.12f, 0.24f, 0.36f};
            float direction[3];
            gfx_metal_get_sun_direction(direction);
            sun_direction_and_intensity = (vector_float4){direction[0], direction[1], direction[2],
                intensities[gfx_metal_sun_intensity() % 4u]};
        }
    }
    bool requires_scene_normals = false;
    for (size_t i = 0; i < graph->count; ++i)
        if (graph->passes[i].resource_flags & GFX_SHADER64_RESOURCE_SCENE_NORMALS)
            requires_scene_normals = true;
    if (requires_scene_normals && (!metal_scene_normals_texture || !metal_scene_normals_valid_for_frame)) {
        static uint32_t normal_missing_logs;
        if (normal_missing_logs++ < 8)
            gfx_ios_diag_log("SHADER64 sceneNormals missing after reconstruction request");
        gfx_metal_shader64_graph_release(graph);
        return false;
    }
    vector_float4 shadow_right_min = {0, 0, 0, 0};
    vector_float4 shadow_up_min = {0, 0, 0, 0};
    vector_float4 shadow_forward_min = {0, 0, 0, 0};
    vector_float4 shadow_ranges = {1, 1, 1, 0};
    if (requires_shadows) {
        if (!metal_shadow_texture || !metal_shadow_transform_valid || !capture.has_view_matrix) {
            static uint32_t shadow_unavailable_logs;
            if (shadow_unavailable_logs++ < 8)
                gfx_ios_diag_log("SHADER64 scene shadow unavailable this frame texture=%d transform=%d view=%d; using native composite",
                    metal_shadow_texture != nil, metal_shadow_transform_valid, capture.has_view_matrix);
            gfx_metal_shader64_graph_release(graph);
            return false;
        }
        shadow_right_min = (vector_float4){ metal_shadow_transform.right[0], metal_shadow_transform.right[1],
            metal_shadow_transform.right[2], metal_shadow_transform.minimum[0] };
        shadow_up_min = (vector_float4){ metal_shadow_transform.up[0], metal_shadow_transform.up[1],
            metal_shadow_transform.up[2], metal_shadow_transform.minimum[1] };
        shadow_forward_min = (vector_float4){ metal_shadow_transform.forward[0], metal_shadow_transform.forward[1],
            metal_shadow_transform.forward[2], metal_shadow_transform.minimum[2] };
        shadow_ranges = (vector_float4){ metal_shadow_transform.range[0], metal_shadow_transform.range[1],
            metal_shadow_transform.range[2], 0.0025f };
    }
    uint64_t target_bytes = 0;
    for (size_t i = 0; i < graph->count; ++i) if (strcmp(graph->passes[i].output, "present")) {
        unsigned int w = 0, h = 0;
        if (scene_source.width > UINT_MAX || scene_source.height > UINT_MAX ||
            !gfx_shader64_graph_target_size((unsigned int)scene_source.width,
                (unsigned int)scene_source.height, graph->passes[i].scale, &w, &h)) {
            ok = false; break;
        }
        uint64_t bytes = (uint64_t)w * h * 8u;
        if (bytes > GFX_SHADER64_MAX_GRAPH_TARGET_BYTES ||
            target_bytes > GFX_SHADER64_MAX_GRAPH_TARGET_BYTES - bytes) { ok = false; break; }
        target_bytes += bytes;
    }
    for (size_t step = 0; ok && step < graph->count; ++step) {
        struct MetalShader64GraphPass *p = &graph->passes[graph->order[step]];
        id<MTLTexture> input = !strcmp(p->input, "sceneColor") ? scene_source : nil;
        if (!input) for (size_t j = 0; j < graph->count; ++j)
            if (!strcmp(graph->passes[j].output, p->input)) { input = graph->passes[j].target; break; }
        BOOL final = !strcmp(p->output, "present");
        unsigned int target_width = 0, target_height = 0;
        if (!final && (scene_source.width > UINT_MAX || scene_source.height > UINT_MAX ||
            !gfx_shader64_graph_target_size((unsigned int)scene_source.width,
                (unsigned int)scene_source.height, p->scale, &target_width, &target_height))) {
            ok = false; break;
        }
        NSUInteger width = final ? drawable.texture.width : target_width;
        NSUInteger height = final ? drawable.texture.height : target_height;
        id<MTLTexture> output = final ? drawable.texture : gfx_metal_shader64_target(p, width, height);
        if (!input || !output) { ok = false; break; }
        if (p->node_type == GFX_SHADER64_NODE_COMPUTE) {
            if (!p->compute_pipeline || final ||
                ((p->resource_flags & GFX_SHADER64_RESOURCE_PACK_TEXTURES) && !graph->pack_texture_count) ||
                ((p->resource_flags & GFX_SHADER64_RESOURCE_SCENE_NORMALS) &&
                 (!metal_scene_normals_texture || !metal_scene_normals_valid_for_frame))) {
                ok = false; break;
            }
            id<MTLComputeCommandEncoder> compute = [command computeCommandEncoder];
            if (!compute) { ok = false; break; }
            [compute setComputePipelineState:p->compute_pipeline];
            [compute setTexture:input atIndex:0];
            [compute setTexture:output atIndex:1];
            [compute setBytes:&parameters length:sizeof(parameters) atIndex:0];
            [compute setBytes:&frame_info length:sizeof(frame_info) atIndex:2];
            if (p->resource_flags & GFX_SHADER64_RESOURCE_SCENE_DEPTH) {
                [compute setTexture:metal_depth_texture atIndex:6];
                [compute setSamplerState:metal_present_sampler atIndex:6];
                [compute setBytes:inverse_projection length:sizeof(inverse_projection) atIndex:3];
            }
            if (p->resource_flags & GFX_SHADER64_RESOURCE_SCENE_NORMALS) {
                [compute setTexture:metal_scene_normals_texture atIndex:7];
                [compute setSamplerState:metal_present_sampler atIndex:7];
            }
            if (p->resource_flags & GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT) {
                [compute setBytes:inverse_view length:sizeof(inverse_view) atIndex:4];
                [compute setBytes:&sun_direction_and_intensity length:sizeof(sun_direction_and_intensity) atIndex:5];
            }
            if (p->resource_flags & GFX_SHADER64_RESOURCE_PACK_TEXTURES) {
                for (NSUInteger texture_index = 0; texture_index < GFX_SHADER64_MAX_PACK_TEXTURES; ++texture_index) {
                    id<MTLTexture> texture = graph->pack_textures[MIN(texture_index,
                        graph->pack_texture_count - 1u)];
                    [compute setTexture:texture atIndex:2u + texture_index];
                }
                uint32_t texture_count = (uint32_t)graph->pack_texture_count;
                [compute setBytes:&texture_count length:sizeof(texture_count) atIndex:1];
            }
            NSUInteger thread_width = MAX(1u, p->compute_pipeline.threadExecutionWidth);
            NSUInteger thread_height = MAX(1u, p->compute_pipeline.maxTotalThreadsPerThreadgroup / thread_width);
            [compute dispatchThreads:MTLSizeMake(width, height, 1)
                threadsPerThreadgroup:MTLSizeMake(thread_width, thread_height, 1)];
            [compute endEncoding];
            continue;
        }
        if (!p->pipeline) { ok = false; break; }
        MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        descriptor.colorAttachments[0].texture = output;
        descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:descriptor];
        if (!encoder) { ok = false; break; }
        [encoder setRenderPipelineState:p->pipeline];
        [encoder setFragmentTexture:input atIndex:0];
        [encoder setFragmentSamplerState:metal_present_sampler atIndex:0];
        if (p->resource_flags & (GFX_SHADER64_RESOURCE_SCENE_DEPTH |
                                 GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT |
                                 GFX_SHADER64_RESOURCE_SHADOW_MAP)) {
            [encoder setFragmentTexture:metal_depth_texture atIndex:1];
            [encoder setFragmentSamplerState:metal_present_sampler atIndex:1];
            [encoder setFragmentBytes:inverse_projection length:sizeof(inverse_projection) atIndex:1];
        }
        if (p->resource_flags & GFX_SHADER64_RESOURCE_SCENE_NORMALS) {
            [encoder setFragmentTexture:metal_scene_normals_texture atIndex:1];
            [encoder setFragmentSamplerState:metal_present_sampler atIndex:1];
        }
        if (p->resource_flags & (GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT |
                                 GFX_SHADER64_RESOURCE_SHADOW_MAP)) {
            [encoder setFragmentBytes:inverse_view length:sizeof(inverse_view) atIndex:2];
            [encoder setFragmentBytes:&sun_direction_and_intensity length:sizeof(sun_direction_and_intensity) atIndex:3];
        }
        if (p->resource_flags & GFX_SHADER64_RESOURCE_SHADOW_MAP) {
            [encoder setFragmentTexture:metal_shadow_texture atIndex:2];
            [encoder setFragmentSamplerState:metal_present_sampler atIndex:2];
            [encoder setFragmentBytes:&shadow_right_min length:sizeof(shadow_right_min) atIndex:4];
            [encoder setFragmentBytes:&shadow_up_min length:sizeof(shadow_up_min) atIndex:5];
            [encoder setFragmentBytes:&shadow_forward_min length:sizeof(shadow_forward_min) atIndex:6];
            [encoder setFragmentBytes:&shadow_ranges length:sizeof(shadow_ranges) atIndex:7];
        }
        if (p->resource_flags & GFX_SHADER64_RESOURCE_PACK_TEXTURES) {
            for (NSUInteger texture_index = 0; texture_index < 4; ++texture_index) {
                id<MTLTexture> texture = graph->pack_textures[MIN(texture_index,
                    graph->pack_texture_count - 1u)];
                [encoder setFragmentTexture:texture atIndex:4u + texture_index];
                [encoder setFragmentSamplerState:metal_present_sampler atIndex:4u + texture_index];
            }
            uint32_t texture_count = (uint32_t)graph->pack_texture_count;
            [encoder setFragmentBytes:&texture_count length:sizeof(texture_count) atIndex:1];
        }
        if (p->resource_flags & GFX_SHADER64_RESOURCE_FRAME_INFO)
            [encoder setFragmentBytes:&frame_info length:sizeof(frame_info) atIndex:8];
        [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
    }
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (void)completed; gfx_metal_shader64_graph_release(graph);
    }];
    if (!ok) {
        gfx_ios_diag_log("SHADER64 graph execution or target budget failed; using built-in composite");
        /* Drop the incompatible candidate after this frame. The command-buffer
         * completion handler keeps its already encoded resources alive. */
        gfx_metal_use_classic_shader();
    }
    return ok;
}

bool gfx_metal_load_shader64_graph_with_material(const char *pack_id,
                                   const struct GfxShader64GraphSource *passes,
                                   size_t pass_count, const char *material_source,
                                   size_t material_source_size, bool material_pack_textures,
                                   const float parameters[4],
                                   char *error_out, size_t error_size) {
    struct GfxShader64GraphPass descriptions[GFX_SHADER64_MAX_GRAPH_PASSES];
    struct MetalShader64Graph *graph;
    if (!metal_device || !metal_view || !gfx_metal_shader64_valid_id(pack_id) || !passes ||
        ((material_source == NULL) != (material_source_size == 0)) ||
        (material_pack_textures && !material_source) ||
        !pass_count || pass_count > GFX_SHADER64_MAX_GRAPH_PASSES) {
        if (error_out && error_size) snprintf(error_out, error_size, "invalid API v2 graph or Metal device unavailable");
        return false;
    }
    for (size_t i = 0; i < pass_count; ++i)
        descriptions[i] = (struct GfxShader64GraphPass){passes[i].id, passes[i].input, passes[i].output,
            passes[i].resolution_scale, passes[i].node_type, passes[i].resource_flags};
    graph = calloc(1, sizeof(*graph));
    if (!graph) { if (error_out && error_size) snprintf(error_out, error_size, "out of memory preparing graph"); return false; }
    graph->references = 1; graph->count = pass_count;
    graph->material_pack_textures = material_pack_textures;
    if (material_source) {
        char validation[192] = {0};
        bool material_valid = material_pack_textures ?
            gfx_shader64_validate_material_texture_source(material_source, material_source_size,
                                                          validation, sizeof(validation)) :
            gfx_shader64_validate_material_source(material_source, material_source_size,
                                                  validation, sizeof(validation));
        if (!material_valid) {
            if (error_out && error_size) snprintf(error_out, error_size, "material hook: %.150s", validation);
            gfx_metal_shader64_graph_release(graph); return false;
        }
        graph->material_source = malloc(material_source_size + 1u);
        if (!graph->material_source) {
            if (error_out && error_size) snprintf(error_out, error_size, "out of memory retaining material hook");
            gfx_metal_shader64_graph_release(graph); return false;
        }
        memcpy(graph->material_source, material_source, material_source_size);
        graph->material_source[material_source_size] = '\0';
        graph->material_source_size = material_source_size;
    }
    unsigned int width = (unsigned int)MAX(1.0, metal_view.drawableSize.width);
    unsigned int height = (unsigned int)MAX(1.0, metal_view.drawableSize.height);
    if (!gfx_shader64_validate_graph(descriptions, pass_count, width, height, graph->order, error_out, error_size)) {
        gfx_metal_shader64_graph_release(graph); return false;
    }
    bool uses_pack_textures = material_pack_textures;
    for (size_t i = 0; i < pass_count; ++i)
        if (passes[i].resource_flags & GFX_SHADER64_RESOURCE_PACK_TEXTURES) uses_pack_textures = true;
    if (uses_pack_textures && !gfx_metal_shader64_load_pack_textures(pack_id, graph, error_out, error_size)) {
        gfx_metal_shader64_graph_release(graph); return false;
    }
    for (size_t i = 0; i < pass_count; ++i) {
        struct MetalShader64GraphPass *stored = &graph->passes[i];
        if (passes[i].node_type == GFX_SHADER64_NODE_COMPUTE) {
            bool compute_pack_textures = (passes[i].resource_flags & GFX_SHADER64_RESOURCE_PACK_TEXTURES) != 0u;
            bool compute_scene_depth = (passes[i].resource_flags & GFX_SHADER64_RESOURCE_SCENE_DEPTH) != 0u;
            bool compute_scene_normals = (passes[i].resource_flags & GFX_SHADER64_RESOURCE_SCENE_NORMALS) != 0u;
            bool compute_scene_lighting = (passes[i].resource_flags & GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT) != 0u;
            char validation[192] = {0};
            bool compute_valid = gfx_shader64_validate_compute_resource_pass_source(
                passes[i].source, passes[i].source_size, compute_pack_textures,
                compute_scene_depth, compute_scene_normals, compute_scene_lighting,
                validation, sizeof(validation));
            if (!compute_valid) {
                if (error_out && error_size) snprintf(error_out, error_size, "compute %.32s: %.150s", passes[i].id ?: "?", validation);
                gfx_metal_shader64_graph_release(graph); return false;
            }
            NSString *source = [[[NSString alloc] initWithBytes:passes[i].source length:passes[i].source_size
                                                       encoding:NSUTF8StringEncoding] autorelease];
            NSError *compile_error = nil;
            id<MTLLibrary> library = source ? [metal_device newLibraryWithSource:source options:nil error:&compile_error] : nil;
            id<MTLFunction> function = [library newFunctionWithName:@"shader64_compute"];
            NSError *pipeline_error = nil;
            MTLComputePipelineReflection *reflection = nil;
            id<MTLComputePipelineState> pipeline = function ?
                [metal_device newComputePipelineStateWithFunction:function
                                                           options:MTLPipelineOptionArgumentInfo
                                                        reflection:&reflection
                                                             error:&pipeline_error] : nil;
            [function release];
            BOOL input_bound = NO, output_bound = NO, parameters_bound = NO, frame_info_bound = NO;
            BOOL texture_count_bound = NO, scene_depth_bound = NO, depth_sampler_bound = NO;
            BOOL inverse_projection_bound = NO;
            BOOL scene_normals_bound = NO, normals_sampler_bound = NO;
            BOOL inverse_view_bound = NO, sun_direction_bound = NO;
            BOOL pack_texture_bound[GFX_SHADER64_MAX_PACK_TEXTURES] = { NO };
            if (pipeline && reflection.arguments.count == (compute_pack_textures ? 9u : 4u) +
                                                               (compute_scene_depth ? 3u : 0u) +
                                                               (compute_scene_normals ? 2u : 0u) +
                                                               (compute_scene_lighting ? 2u : 0u)) {
                for (MTLArgument *argument in reflection.arguments) {
                    if (argument.type == MTLArgumentTypeTexture && argument.textureType == MTLTextureType2D &&
                        argument.index == 0u && argument.access == MTLArgumentAccessReadOnly) input_bound = YES;
                    else if (argument.type == MTLArgumentTypeTexture && argument.textureType == MTLTextureType2D &&
                             argument.index == 1u && argument.access == MTLArgumentAccessWriteOnly) output_bound = YES;
                    else if (argument.type == MTLArgumentTypeBuffer && argument.index == 0u) parameters_bound = YES;
                    else if (argument.type == MTLArgumentTypeBuffer && argument.index == 2u) frame_info_bound = YES;
                    else if (compute_pack_textures && argument.type == MTLArgumentTypeBuffer && argument.index == 1u)
                        texture_count_bound = YES;
                    else if (compute_pack_textures && argument.type == MTLArgumentTypeTexture &&
                             argument.textureType == MTLTextureType2D && argument.access == MTLArgumentAccessReadOnly &&
                             argument.index >= 2u && argument.index < 2u + GFX_SHADER64_MAX_PACK_TEXTURES)
                        pack_texture_bound[argument.index - 2u] = YES;
                    else if (compute_scene_depth && argument.type == MTLArgumentTypeTexture &&
                             argument.textureType == MTLTextureType2D && argument.access == MTLArgumentAccessReadOnly &&
                             argument.index == 6u) scene_depth_bound = YES;
                    else if (compute_scene_depth && argument.type == MTLArgumentTypeSampler && argument.index == 6u)
                        depth_sampler_bound = YES;
                    else if (compute_scene_depth && argument.type == MTLArgumentTypeBuffer && argument.index == 3u)
                        inverse_projection_bound = YES;
                    else if (compute_scene_normals && argument.type == MTLArgumentTypeTexture &&
                             argument.textureType == MTLTextureType2D && argument.access == MTLArgumentAccessReadOnly &&
                             argument.index == 7u) scene_normals_bound = YES;
                    else if (compute_scene_normals && argument.type == MTLArgumentTypeSampler && argument.index == 7u)
                        normals_sampler_bound = YES;
                    else if (compute_scene_lighting && argument.type == MTLArgumentTypeBuffer && argument.index == 4u)
                        inverse_view_bound = YES;
                    else if (compute_scene_lighting && argument.type == MTLArgumentTypeBuffer && argument.index == 5u)
                        sun_direction_bound = YES;
                }
            }
            BOOL abi_valid = input_bound && output_bound && parameters_bound && frame_info_bound;
            if (compute_pack_textures) {
                abi_valid = abi_valid && texture_count_bound;
                for (NSUInteger texture_index = 0; texture_index < GFX_SHADER64_MAX_PACK_TEXTURES; ++texture_index)
                    abi_valid = abi_valid && pack_texture_bound[texture_index];
            }
            if (compute_scene_depth)
                abi_valid = abi_valid && scene_depth_bound && depth_sampler_bound && inverse_projection_bound;
            if (compute_scene_normals)
                abi_valid = abi_valid && scene_normals_bound && normals_sampler_bound;
            if (compute_scene_lighting)
                abi_valid = abi_valid && inverse_view_bound && sun_direction_bound;
            if (!library || !pipeline) {
                const char *message = (pipeline_error ?: compile_error).localizedDescription.UTF8String ?: "Metal rejected compute pass";
                if (error_out && error_size) snprintf(error_out, error_size, "compute %.32s failed: %.300s", passes[i].id, message);
                [pipeline release]; [library release]; gfx_metal_shader64_graph_release(graph); return false;
            }
            if (!abi_valid) {
                if (error_out && error_size) snprintf(error_out, error_size,
                    "compute %.32s resource bindings do not match the API 2 contract", passes[i].id);
                [pipeline release]; [library release]; gfx_metal_shader64_graph_release(graph); return false;
            }
            snprintf(stored->id, sizeof(stored->id), "%s", passes[i].id);
            snprintf(stored->input, sizeof(stored->input), "%s", passes[i].input);
            snprintf(stored->output, sizeof(stored->output), "%s", passes[i].output);
            stored->scale = passes[i].resolution_scale;
            stored->node_type = GFX_SHADER64_NODE_COMPUTE;
            stored->resource_flags = passes[i].resource_flags;
            stored->library = library;
            stored->compute_pipeline = pipeline;
            continue;
        }
        char validation[192] = {0};
        bool has_pack_textures = (passes[i].resource_flags & GFX_SHADER64_RESOURCE_PACK_TEXTURES) != 0u;
        bool has_scene_normals = (passes[i].resource_flags & GFX_SHADER64_RESOURCE_SCENE_NORMALS) != 0u;
        bool has_frame_info = (passes[i].resource_flags & GFX_SHADER64_RESOURCE_FRAME_INFO) != 0u;
        bool source_valid;
        if (has_frame_info)
            source_valid = gfx_shader64_validate_frame_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        else if (has_pack_textures && has_scene_normals)
            source_valid = gfx_shader64_validate_normals_textured_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        else if (has_pack_textures)
            source_valid = gfx_shader64_validate_textured_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        else if (has_scene_normals)
            source_valid = gfx_shader64_validate_normals_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        else if (passes[i].resource_flags & GFX_SHADER64_RESOURCE_SHADOW_MAP)
            source_valid = gfx_shader64_validate_shadow_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        else if (passes[i].resource_flags & GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT)
            source_valid = gfx_shader64_validate_lit_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        else if (passes[i].resource_flags & GFX_SHADER64_RESOURCE_SCENE_DEPTH)
            source_valid = gfx_shader64_validate_depth_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        else
            source_valid = gfx_shader64_validate_pass_source(passes[i].source,
                passes[i].source_size, validation, sizeof(validation));
        if (!source_valid) {
            if (error_out && error_size) snprintf(error_out, error_size, "pass %.32s: %.150s", passes[i].id ?: "?", validation);
            gfx_metal_shader64_graph_release(graph); return false;
        }
        size_t capacity = GFX_SHADER64_MAX_SOURCE_BYTES + 2048u;
        char *wrapper = malloc(capacity);
        bool wrapper_ok = false;
        if (wrapper) {
            if (has_frame_info)
                wrapper_ok = gfx_shader64_build_frame_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
            else if (has_pack_textures && has_scene_normals)
                wrapper_ok = gfx_shader64_build_normals_textured_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
            else if (has_pack_textures)
                wrapper_ok = gfx_shader64_build_textured_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
            else if (has_scene_normals)
                wrapper_ok = gfx_shader64_build_normals_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
            else if (passes[i].resource_flags & GFX_SHADER64_RESOURCE_SHADOW_MAP)
                wrapper_ok = gfx_shader64_build_shadow_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
            else if (passes[i].resource_flags & GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT)
                wrapper_ok = gfx_shader64_build_lit_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
            else if (passes[i].resource_flags & GFX_SHADER64_RESOURCE_SCENE_DEPTH)
                wrapper_ok = gfx_shader64_build_depth_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
            else
                wrapper_ok = gfx_shader64_build_pass_wrapper(passes[i].source,
                    passes[i].source_size, wrapper, capacity);
        }
        if (!wrapper_ok) {
            free(wrapper); if (error_out && error_size) snprintf(error_out, error_size, "could not wrap pass source");
            gfx_metal_shader64_graph_release(graph); return false;
        }
        NSString *text = [[[NSString alloc] initWithBytes:wrapper length:strlen(wrapper) encoding:NSUTF8StringEncoding] autorelease];
        free(wrapper);
        NSError *compile_error = nil;
        id<MTLLibrary> library = text ? [metal_device newLibraryWithSource:text options:nil error:&compile_error] : nil;
        id<MTLFunction> vertex = [library newFunctionWithName:@"sm64_shader64_vertex"];
        id<MTLFunction> fragment = [library newFunctionWithName:@"sm64_shader64_fragment"];
        MTLRenderPipelineDescriptor *descriptor = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        descriptor.vertexFunction = vertex; descriptor.fragmentFunction = fragment;
        descriptor.colorAttachments[0].pixelFormat = !strcmp(passes[i].output, "present") ? metal_view.colorPixelFormat : MTLPixelFormatRGBA16Float;
        descriptor.sampleCount = 1;
        NSError *pipeline_error = nil;
        id<MTLRenderPipelineState> pipeline = vertex && fragment ? [metal_device newRenderPipelineStateWithDescriptor:descriptor error:&pipeline_error] : nil;
        [vertex release]; [fragment release];
        if (!library || !pipeline) {
            const char *message = (pipeline_error ?: compile_error).localizedDescription.UTF8String ?: "Metal rejected pass";
            if (error_out && error_size) snprintf(error_out, error_size, "pass %.32s failed: %.300s", passes[i].id, message);
            [pipeline release]; [library release]; gfx_metal_shader64_graph_release(graph); return false;
        }
        snprintf(stored->id, sizeof(stored->id), "%s", passes[i].id);
        snprintf(stored->input, sizeof(stored->input), "%s", passes[i].input);
        snprintf(stored->output, sizeof(stored->output), "%s", passes[i].output);
        stored->scale = passes[i].resolution_scale; stored->library = library; stored->pipeline = pipeline;
        stored->node_type = GFX_SHADER64_NODE_FULLSCREEN;
        stored->resource_flags = passes[i].resource_flags;
    }
    struct MetalShader64Graph *old;
    @synchronized ([NSObject class]) {
        old = metal_shader64_graph; metal_shader64_graph = graph;
        ++metal_material_generation;
        if (!metal_material_generation) metal_material_generation = 1;
        [metal_shader64_pipeline release]; metal_shader64_pipeline = nil;
        [metal_shader64_library release]; metal_shader64_library = nil;
        snprintf(metal_shader64_active_id, sizeof(metal_shader64_active_id), "%s", pack_id);
        metal_shader64_parameters = (vector_float4){
            parameters && isfinite(parameters[0]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[0])) : 1,
            parameters && isfinite(parameters[1]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[1])) : 0,
            parameters && isfinite(parameters[2]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[2])) : 0,
            parameters && isfinite(parameters[3]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[3])) : 0};
        metal_offscreen_runtime_failed = false;
    }
    gfx_metal_shader64_graph_release(old);
    [[NSUserDefaults standardUserDefaults] setObject:[NSString stringWithUTF8String:pack_id] forKey:@"SM64Shader64ActivePack"];
    if (error_out && error_size) error_out[0] = '\0';
    gfx_ios_diag_log("SHADER64 API v2 graph activated id=%s passes=%lu", pack_id, (unsigned long)pass_count);
    return true;
}

bool gfx_metal_load_shader64_graph(const char *pack_id,
                                   const struct GfxShader64GraphSource *passes,
                                   size_t pass_count, const float parameters[4],
                                   char *error_out, size_t error_size) {
    return gfx_metal_load_shader64_graph_with_material(pack_id, passes, pass_count,
        NULL, 0, false, parameters, error_out, error_size);
}

bool gfx_metal_shader64_needs_scene_depth(void) {
    bool needed = false;
    @synchronized ([NSObject class]) {
        if (metal_shader64_graph) for (size_t i = 0; i < metal_shader64_graph->count; ++i)
            if (metal_shader64_graph->passes[i].resource_flags &
                (GFX_SHADER64_RESOURCE_SCENE_DEPTH | GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT |
                 GFX_SHADER64_RESOURCE_SHADOW_MAP | GFX_SHADER64_RESOURCE_SCENE_NORMALS)) {
                needed = true;
                break;
            }
    }
    return needed;
}

static bool gfx_metal_shader64_requires_shadows(void) {
    bool needed = false;
    @synchronized ([NSObject class]) {
        if (metal_shader64_graph) for (size_t i = 0; i < metal_shader64_graph->count; ++i)
            if (metal_shader64_graph->passes[i].resource_flags & GFX_SHADER64_RESOURCE_SHADOW_MAP) {
                needed = true;
                break;
            }
    }
    return needed;
}

static bool gfx_metal_activate_shader64_library(const char *pack_id, id<MTLLibrary> library,
                                                const float parameters[4], char *error_out,
                                                size_t error_size) {
    NSError *error = nil;
    id<MTLFunction> vertex = [library newFunctionWithName:@"sm64_shader64_vertex"];
    id<MTLFunction> fragment = [library newFunctionWithName:@"sm64_shader64_fragment"];
    MTLRenderPipelineDescriptor *descriptor = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = metal_view.colorPixelFormat;
    descriptor.sampleCount = 1;
    id<MTLRenderPipelineState> pipeline = (vertex && fragment) ?
        [metal_device newRenderPipelineStateWithDescriptor:descriptor error:&error] : nil;
    [vertex release]; [fragment release];
    if (!pipeline) {
        const char *message = error.localizedDescription.UTF8String ?: "required SHADER64 entry points are missing";
        if (error_out && error_size) snprintf(error_out, error_size, "Metal pipeline creation failed: %.400s", message);
        gfx_ios_diag_log("SHADER64 pipeline failed pack=%s message=%s", pack_id, message);
        [library release];
        return false;
    }
    struct MetalShader64Graph *old_graph = NULL;
    @synchronized ([NSObject class]) {
        old_graph = metal_shader64_graph;
        metal_shader64_graph = NULL;
        ++metal_material_generation;
        if (!metal_material_generation) metal_material_generation = 1;
        [metal_shader64_pipeline release];
        [metal_shader64_library release];
        metal_shader64_pipeline = pipeline;
        metal_shader64_library = library;
        snprintf(metal_shader64_active_id, sizeof(metal_shader64_active_id), "%s", pack_id);
        metal_shader64_parameters = (vector_float4){
            parameters && isfinite(parameters[0]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[0])) : 1.0f,
            parameters && isfinite(parameters[1]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[1])) : 0.0f,
            parameters && isfinite(parameters[2]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[2])) : 0.0f,
            parameters && isfinite(parameters[3]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[3])) : 0.0f
        };
        metal_offscreen_runtime_failed = false;
    }
    gfx_metal_shader64_graph_release(old_graph);
    if (error_out && error_size) error_out[0] = '\0';
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:[NSString stringWithUTF8String:pack_id] forKey:@"SM64Shader64ActivePack"];
    [defaults setObject:@[@(metal_shader64_parameters.x), @(metal_shader64_parameters.y),
                          @(metal_shader64_parameters.z), @(metal_shader64_parameters.w)]
                  forKey:@"SM64Shader64Parameters"];
    gfx_ios_diag_log("SHADER64 pack activated id=%s stage=postprocess api=1", pack_id);
    return true;
}

bool gfx_metal_load_shader64_source(const char *pack_id, const char *source,
                                    size_t source_size, const float parameters[4], char *error_out,
                                    size_t error_size) {
    char validation[192] = {0};
    if (!metal_device || !metal_view || !gfx_metal_shader64_valid_id(pack_id) ||
        !gfx_shader64_validate_post_source(source, source_size, validation, sizeof(validation))) {
        const char *message = validation[0] ? validation : "invalid pack id or Metal device unavailable";
        if (error_out && error_size) snprintf(error_out, error_size, "%s", message);
        return false;
    }
    size_t wrapper_capacity = GFX_SHADER64_MAX_SOURCE_BYTES + 1024u;
    char *wrapper_bytes = malloc(wrapper_capacity);
    if (!wrapper_bytes || !gfx_shader64_build_wrapper(source, source_size, wrapper_bytes, wrapper_capacity)) {
        free(wrapper_bytes);
        if (error_out && error_size) snprintf(error_out, error_size, "could not construct the Shader64 API v1 wrapper");
        return false;
    }
    NSString *wrapped = [[[NSString alloc] initWithBytes:wrapper_bytes
        length:strlen(wrapper_bytes) encoding:NSUTF8StringEncoding] autorelease];
    free(wrapper_bytes);
    if (!wrapped) {
        if (error_out && error_size) snprintf(error_out, error_size, "shader source is not valid UTF-8");
        return false;
    }
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithSource:wrapped options:nil error:&error];
    if (!library) {
        if (error_out && error_size) snprintf(error_out, error_size, "Metal compile failed: %.400s", error.localizedDescription.UTF8String ?: "unknown error");
        gfx_ios_diag_log("SHADER64 compile failed pack=%s message=%s", pack_id, error.localizedDescription.UTF8String ?: "unknown");
        return false;
    }
    return gfx_metal_activate_shader64_library(pack_id, library, parameters, error_out, error_size);
}

bool gfx_metal_load_shader64_library(const char *pack_id, const void *library_bytes,
                                     size_t library_size, const float parameters[4], char *error_out,
                                     size_t error_size) {
    if (!metal_device || !metal_view || !gfx_metal_shader64_valid_id(pack_id) ||
        !library_bytes || !library_size || library_size > GFX_SHADER64_MAX_LIBRARY_BYTES) {
        if (error_out && error_size) snprintf(error_out, error_size, "invalid pack id, Metal device, or compiled library size");
        return false;
    }
    dispatch_data_t bytes = dispatch_data_create(library_bytes, library_size,
        DISPATCH_TARGET_QUEUE_DEFAULT, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    if (!bytes) {
        if (error_out && error_size) snprintf(error_out, error_size, "could not create Metal library data handle");
        return false;
    }
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithData:bytes error:&error];
#if OS_OBJECT_USE_OBJC
    [bytes release];
#else
    dispatch_release(bytes);
#endif
    if (!library) {
        const char *message = error.localizedDescription.UTF8String ?: "unknown library format or target";
        if (error_out && error_size) snprintf(error_out, error_size, "compiled Metal library rejected: %.400s", message);
        gfx_ios_diag_log("SHADER64 library rejected pack=%s message=%s", pack_id, message);
        return false;
    }
    return gfx_metal_activate_shader64_library(pack_id, library, parameters, error_out, error_size);
}

static void gfx_metal_restore_shader64_pack(void) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *packID = [defaults stringForKey:@"SM64Shader64ActivePack"];
    if (!packID.length || !gfx_metal_shader64_valid_id(packID.UTF8String)) return;
    NSArray<NSString *> *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (!documents.count) return;
    NSString *packRoot = [[documents[0] stringByAppendingPathComponent:@"shaderpacks"] stringByAppendingPathComponent:packID];
    NSData *manifestData = [NSData dataWithContentsOfFile:[packRoot stringByAppendingPathComponent:@"manifest.json"]];
    NSDictionary *manifest = manifestData ? [NSJSONSerialization JSONObjectWithData:manifestData options:0 error:nil] : nil;
    if ([manifest[@"shaderApiVersion"] integerValue] == 2 &&
        [manifest[@"passes"] isKindOfClass:[NSArray class]] &&
        [manifest[@"passes"] count] > 0 && [manifest[@"passes"] count] <= GFX_SHADER64_MAX_GRAPH_PASSES) {
        NSArray *definitions = manifest[@"passes"];
        NSMutableArray<NSData *> *sources = [NSMutableArray array];
        NSData *material_source = nil;
        struct GfxShader64GraphSource graph_passes[GFX_SHADER64_MAX_GRAPH_PASSES];
        memset(graph_passes, 0, sizeof(graph_passes));
        BOOL valid = [manifest[@"formatVersion"] integerValue] == 2 && [manifest[@"stage"] isEqualToString:@"render_graph"];
        for (NSUInteger i = 0; valid && i < definitions.count; ++i) {
            NSDictionary *definition = definitions[i];
            if (![definition isKindOfClass:[NSDictionary class]] ||
                (definition[@"sceneDepth"] &&
                 ![definition[@"sceneDepth"] isKindOfClass:[NSNumber class]]) ||
                (definition[@"sceneLighting"] &&
                 ![definition[@"sceneLighting"] isKindOfClass:[NSNumber class]]) ||
                (definition[@"sceneShadows"] &&
                 ![definition[@"sceneShadows"] isKindOfClass:[NSNumber class]]) ||
                (definition[@"sceneNormals"] &&
                 ![definition[@"sceneNormals"] isKindOfClass:[NSNumber class]]) ||
                (definition[@"frameInfo"] &&
                 ![definition[@"frameInfo"] isKindOfClass:[NSNumber class]]) ||
                (definition[@"packTextures"] &&
                 ![definition[@"packTextures"] isKindOfClass:[NSNumber class]])) { valid = NO; break; }
            NSString *relative = definition[@"source"];
            if (![relative isKindOfClass:[NSString class]] || ![relative hasPrefix:@"shaders/"] ||
                [relative containsString:@".."] || [relative.pathComponents count] != 2 || ![relative hasSuffix:@".metal"]) { valid = NO; break; }
            NSString *path = [packRoot stringByAppendingPathComponent:relative];
            NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
            NSData *source_data = [attributes[NSFileType] isEqualToString:NSFileTypeRegular] &&
                [attributes[NSFileSize] unsignedLongLongValue] <= GFX_SHADER64_MAX_SOURCE_BYTES ? [NSData dataWithContentsOfFile:path] : nil;
            if (!source_data) { valid = NO; break; }
            [sources addObject:source_data];
            NSString *type = definition[@"type"];
            if (![type isEqualToString:@"fullscreen"] && ![type isEqualToString:@"compute"]) {
                valid = NO; break;
            }
            unsigned int node_type = [type isEqualToString:@"compute"] ?
                GFX_SHADER64_NODE_COMPUTE : GFX_SHADER64_NODE_FULLSCREEN;
            graph_passes[i] = (struct GfxShader64GraphSource){
                [definition[@"id"] UTF8String], [definition[@"input"] UTF8String], [definition[@"output"] UTF8String],
                source_data.bytes, source_data.length, [definition[@"scale"] floatValue], node_type,
                ([definition[@"sceneDepth"] boolValue] ? GFX_SHADER64_RESOURCE_SCENE_DEPTH : 0u) |
                ([definition[@"sceneLighting"] boolValue] ? GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT : 0u) |
                ([definition[@"sceneShadows"] boolValue] ? GFX_SHADER64_RESOURCE_SHADOW_MAP : 0u) |
                ([definition[@"sceneNormals"] boolValue] ? GFX_SHADER64_RESOURCE_SCENE_NORMALS : 0u) |
                ([definition[@"packTextures"] boolValue] ? GFX_SHADER64_RESOURCE_PACK_TEXTURES : 0u) |
                ([definition[@"frameInfo"] boolValue] ? GFX_SHADER64_RESOURCE_FRAME_INFO : 0u) };
        }
        NSDictionary *material_definition = manifest[@"materialShader"];
        if (valid && material_definition &&
            ![material_definition isKindOfClass:[NSDictionary class]]) valid = NO;
        if (valid && material_definition && material_definition[@"packTextures"] &&
            ![material_definition[@"packTextures"] isKindOfClass:[NSNumber class]]) valid = NO;
        if (valid && material_definition) {
            NSString *relative_material = material_definition[@"source"];
            if (![relative_material isKindOfClass:[NSString class]] ||
                ![relative_material hasPrefix:@"shaders/"] || [relative_material containsString:@".."] ||
                [relative_material.pathComponents count] != 2 || ![relative_material hasSuffix:@".metal"]) {
                valid = NO;
            } else {
                NSString *material_path = [packRoot stringByAppendingPathComponent:relative_material];
                NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:material_path error:nil];
                material_source = [attributes[NSFileType] isEqualToString:NSFileTypeRegular] &&
                    [attributes[NSFileSize] unsignedLongLongValue] <= GFX_SHADER64_MAX_SOURCE_BYTES ?
                    [NSData dataWithContentsOfFile:material_path] : nil;
                if (!material_source) valid = NO;
            }
        }
        NSArray *saved = [defaults arrayForKey:@"SM64Shader64Parameters"];
        float parameters[4] = {1, 0, 0, 0};
        if (saved.count <= 4) for (NSUInteger i = 0; i < saved.count; ++i)
            if ([saved[i] isKindOfClass:[NSNumber class]]) parameters[i] = [saved[i] floatValue];
        char error[384] = {0};
        bool material_pack_textures = [material_definition[@"packTextures"] boolValue];
        bool loaded = valid && gfx_metal_load_shader64_graph_with_material(packID.UTF8String,
            graph_passes, sources.count, material_source.bytes, material_source.length,
            material_pack_textures, parameters, error, sizeof(error));
        if (!loaded) {
            [defaults removeObjectForKey:@"SM64Shader64ActivePack"];
            gfx_ios_diag_log("SHADER64 API v2 restore failed id=%s reason=%s; Classic64 retained", packID.UTF8String,
                             error[0] ? error : "graph manifest or pass source invalid");
        }
        return;
    }
    NSString *relative = [manifest[@"source"] isKindOfClass:[NSString class]] ? manifest[@"source"] : nil;
    BOOL compiledLibrary = [relative isEqualToString:@"shaders/postprocess.metallib"];
    if (!compiledLibrary && ![relative isEqualToString:@"shaders/postprocess.metal"]) {
        [defaults removeObjectForKey:@"SM64Shader64ActivePack"];
        gfx_ios_diag_log("SHADER64 restore skipped id=%s reason=manifest source path unsupported", packID.UTF8String);
        return;
    }
    NSString *sourcePath = [packRoot stringByAppendingPathComponent:relative];
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:sourcePath error:nil];
    unsigned long long maxSize = compiledLibrary ? GFX_SHADER64_MAX_LIBRARY_BYTES : GFX_SHADER64_MAX_SOURCE_BYTES;
    if (![attrs[NSFileType] isEqualToString:NSFileTypeRegular] || [attrs[NSFileSize] unsignedLongLongValue] > maxSize) {
        [defaults removeObjectForKey:@"SM64Shader64ActivePack"];
        gfx_ios_diag_log("SHADER64 restore skipped id=%s reason=source missing or oversized", packID.UTF8String);
        return;
    }
    NSData *data = [NSData dataWithContentsOfFile:sourcePath];
    NSArray *saved = [defaults arrayForKey:@"SM64Shader64Parameters"];
    float parameters[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    if (saved.count <= 4) for (NSUInteger i = 0; i < saved.count; ++i)
        if ([saved[i] isKindOfClass:[NSNumber class]]) parameters[i] = [saved[i] floatValue];
    char error[384] = {0};
    bool loaded = data && (compiledLibrary ?
        gfx_metal_load_shader64_library(packID.UTF8String, data.bytes, data.length, parameters, error, sizeof(error)) :
        gfx_metal_load_shader64_source(packID.UTF8String, data.bytes, data.length, parameters, error, sizeof(error)));
    if (!loaded) {
        [defaults removeObjectForKey:@"SM64Shader64ActivePack"];
        gfx_ios_diag_log("SHADER64 restore failed id=%s reason=%s; Classic64 retained", packID.UTF8String,
                         error[0] ? error : "source unavailable");
    }
}

void gfx_metal_use_classic_shader(void) {
    struct MetalShader64Graph *old_graph = NULL;
    @synchronized ([NSObject class]) {
        old_graph = metal_shader64_graph; metal_shader64_graph = NULL;
        ++metal_material_generation;
        if (!metal_material_generation) metal_material_generation = 1;
        [metal_shader64_pipeline release]; metal_shader64_pipeline = nil;
        [metal_shader64_library release]; metal_shader64_library = nil;
        metal_shader64_active_id[0] = '\0';
        metal_shader64_parameters = (vector_float4){ 1.0f, 0.0f, 0.0f, 0.0f };
        metal_offscreen_runtime_failed = false;
    }
    gfx_metal_shader64_graph_release(old_graph);
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"SM64Shader64ActivePack"];
    gfx_ios_diag_log("SHADER64 restored Classic64 renderer");
}

const char *gfx_metal_active_shader64_pack(void) {
    return metal_shader64_active_id[0] ? metal_shader64_active_id : "Classic64";
}

static bool gfx_metal_shader64_is_active(void) {
    @synchronized ([NSObject class]) { return metal_shader64_pipeline != nil || metal_shader64_graph != NULL; }
}

void gfx_metal_set_shader64_parameters(const float parameters[4]) {
    if (!parameters) return;
    vector_float4 value = {0, 0, 0, 0};
    for (NSUInteger i = 0; i < 4; ++i) value[i] = isfinite(parameters[i]) ? fmaxf(-GFX_SHADER64_PARAMETER_ABS_MAX, fminf(GFX_SHADER64_PARAMETER_ABS_MAX, parameters[i])) : 0.0f;
    @synchronized ([NSObject class]) { metal_shader64_parameters = value; }
    [[NSUserDefaults standardUserDefaults] setObject:@[@(value.x), @(value.y), @(value.z), @(value.w)]
        forKey:@"SM64Shader64Parameters"];
}

static id<MTLTexture> gfx_metal_new_target_texture(NSUInteger width, NSUInteger height,
                                                   NSUInteger sample_count,
                                                   MTLPixelFormat pixel_format,
                                                   MTLTextureUsage usage) {
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixel_format
                                                                                          width:width
                                                                                         height:height
                                                                                      mipmapped:NO];
    descriptor.textureType = sample_count > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    descriptor.sampleCount = sample_count;
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = usage;
    return [metal_device newTextureWithDescriptor:descriptor];
}

static bool gfx_metal_ensure_shadow_texture(void) {
    NSUInteger resolution = gfx_metal_shadow_resolution();
    if (metal_shadow_texture && metal_shadow_texture_size == resolution) return true;
    [metal_shadow_texture release]; metal_shadow_texture = nil;
    metal_shadow_texture_size = 0;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                           width:resolution
                                                                                          height:resolution
                                                                                       mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    metal_shadow_texture = [metal_device newTextureWithDescriptor:descriptor];
    if (!metal_shadow_texture) return false;
    metal_shadow_texture.label = @"SM64 directional shadow depth";
    metal_shadow_texture_size = resolution;
    return true;
}

static void gfx_metal_fallback_to_direct(const char *reason) {
    struct GfxMetalSettings fallback = metal_settings;
    fallback.render_scale_percent = 100u;
    fallback.sample_count = 1u;
    gfx_ios_diag_log("Metal offscreen rendering fallback reason=%s; active render target is drawable at 100%%/1x", reason ?: "unknown");
    gfx_metal_apply_settings(fallback);
    @synchronized ([NSObject class]) { metal_offscreen_runtime_failed = true; }
}

static bool gfx_metal_ensure_offscreen_targets(id<CAMetalDrawable> drawable) {
    /* Keep rendering directly to the drawable after an enhanced-path failure.
     * Setting changes clear this latch so the user can retry deliberately. */
    if (metal_offscreen_runtime_failed) return false;
    if (!gfx_metal_settings_use_offscreen(metal_settings) && !gfx_metal_ssao_enabled() &&
        !gfx_metal_fxaa_enabled() && !gfx_metal_post_enabled() && !gfx_metal_fog_enabled() &&
        !gfx_metal_bloom_enabled() && gfx_metal_color_grade() == 0u && gfx_metal_sky_preset() == 0u &&
        !gfx_metal_shadows_enabled() && !gfx_metal_god_rays_enabled() && gfx_metal_sun_intensity() == 0u &&
        !gfx_metal_shader64_is_active()) return false;
    if (!gfx_metal_create_present_pipeline()) {
        gfx_metal_fallback_to_direct("final pass unavailable");
        return false;
    }
    struct GfxMetalTargetSize size = { 0, 0 };
    if (!gfx_metal_target_size((uint32_t)drawable.texture.width,
                               (uint32_t)drawable.texture.height,
                               metal_settings.render_scale_percent, &size)) {
        gfx_metal_fallback_to_direct("invalid drawable dimensions or scale");
        return false;
    }
    NSUInteger samples = (gfx_metal_ssao_enabled() || gfx_metal_shadows_enabled() || gfx_metal_god_rays_enabled() ||
                          gfx_metal_sun_intensity() != 0u || gfx_metal_shader64_needs_scene_depth()) ?
                         1u : metal_settings.sample_count;
    BOOL target_ready = metal_target_width == size.width && metal_target_height == size.height &&
        metal_target_samples == samples && metal_depth_texture != nil &&
        (!gfx_metal_ssao_enabled() || (metal_ssao_texture && metal_ssao_filtered_texture &&
         metal_ssao_width == (size.width + 1u) / 2u &&
         metal_ssao_height == (size.height + 1u) / 2u)) &&
        (!gfx_metal_bloom_enabled() || (metal_bloom_extract_texture && metal_bloom_temp_texture &&
         metal_bloom_output_texture && metal_bloom_width == (size.width + 3u) / 4u &&
         metal_bloom_height == (size.height + 3u) / 4u)) &&
        (!gfx_metal_shadows_enabled() || (metal_shadow_texture && metal_shadow_texture_size == gfx_metal_shadow_resolution())) &&
        (!gfx_metal_god_rays_enabled() || (metal_god_ray_texture &&
         metal_god_ray_width == (size.width + 3u) / 4u && metal_god_ray_height == (size.height + 3u) / 4u)) &&
        (samples > 1 ? (metal_multisample_color && metal_resolve_texture) : metal_color_target != nil);
    if (target_ready) return true;

    gfx_metal_release_render_targets();
    if (gfx_metal_shadows_enabled() && !gfx_metal_ensure_shadow_texture()) {
        gfx_ios_diag_log("Metal directional shadows disabled: depth target allocation failed");
        gfx_metal_set_shadows_enabled(false);
    }
    if (samples > 1) {
        metal_multisample_color = gfx_metal_new_target_texture(size.width, size.height, samples,
        MTLPixelFormatRGBA16Float, MTLTextureUsageRenderTarget);
        metal_resolve_texture = gfx_metal_new_target_texture(size.width, size.height, 1,
            MTLPixelFormatRGBA16Float, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        if (metal_multisample_color) metal_multisample_color.label = @"SM64 MSAA color";
        if (metal_resolve_texture) metal_resolve_texture.label = @"SM64 resolved color";
    } else {
        metal_color_target = gfx_metal_new_target_texture(size.width, size.height, 1,
            MTLPixelFormatRGBA16Float, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        if (metal_color_target) metal_color_target.label = @"SM64 scaled color";
    }
    id<MTLTexture> depth = gfx_metal_depth_for_size(size.width, size.height, samples);
    if (gfx_metal_ssao_enabled()) {
        NSUInteger ao_width = (size.width + 1u) / 2u;
        NSUInteger ao_height = (size.height + 1u) / 2u;
        MTLTextureDescriptor *ao = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                       width:ao_width height:ao_height mipmapped:NO];
        ao.storageMode = MTLStorageModePrivate;
        ao.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        metal_ssao_texture = [metal_device newTextureWithDescriptor:ao];
        metal_ssao_filtered_texture = [metal_device newTextureWithDescriptor:ao];
        if (metal_ssao_texture) {
            metal_ssao_texture.label = @"SM64 SSAO visibility";
        }
        if (metal_ssao_filtered_texture) {
            metal_ssao_filtered_texture.label = @"SM64 SSAO filtered visibility";
        }
        if (metal_ssao_texture && metal_ssao_filtered_texture) {
            metal_ssao_width = ao_width;
            metal_ssao_height = ao_height;
        }
    }
    if (gfx_metal_bloom_enabled()) {
        NSUInteger bloom_width = (size.width + 3u) / 4u;
        NSUInteger bloom_height = (size.height + 3u) / 4u;
        MTLTextureDescriptor *bloom = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                           width:bloom_width height:bloom_height mipmapped:NO];
        bloom.storageMode = MTLStorageModePrivate;
        bloom.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        metal_bloom_extract_texture = [metal_device newTextureWithDescriptor:bloom];
        metal_bloom_temp_texture = [metal_device newTextureWithDescriptor:bloom];
        metal_bloom_output_texture = [metal_device newTextureWithDescriptor:bloom];
        if (metal_bloom_extract_texture && metal_bloom_temp_texture && metal_bloom_output_texture) {
            metal_bloom_width = bloom_width;
            metal_bloom_height = bloom_height;
            metal_bloom_extract_texture.label = @"SM64 bloom bright pass";
            metal_bloom_temp_texture.label = @"SM64 bloom horizontal blur";
            metal_bloom_output_texture.label = @"SM64 bloom vertical blur";
        }
    }
    if (gfx_metal_god_rays_enabled()) {
        NSUInteger ray_width = (size.width + 3u) / 4u;
        NSUInteger ray_height = (size.height + 3u) / 4u;
        MTLTextureDescriptor *rays = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                         width:ray_width height:ray_height mipmapped:NO];
        rays.storageMode = MTLStorageModePrivate;
        rays.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        metal_god_ray_texture = [metal_device newTextureWithDescriptor:rays];
        if (metal_god_ray_texture) {
            metal_god_ray_width = ray_width;
            metal_god_ray_height = ray_height;
            metal_god_ray_texture.label = @"SM64 depth-occluded light shafts";
        }
    }
    if ((samples > 1 && (!metal_multisample_color || !metal_resolve_texture)) ||
        (samples == 1 && !metal_color_target) || !depth ||
        (gfx_metal_ssao_enabled() && (!metal_ssao_texture || !metal_ssao_filtered_texture)) ||
        (gfx_metal_bloom_enabled() && (!metal_bloom_extract_texture || !metal_bloom_temp_texture || !metal_bloom_output_texture)) ||
        (gfx_metal_god_rays_enabled() && !metal_god_ray_texture)) {
        gfx_metal_release_render_targets();
        if (gfx_metal_ssao_enabled()) gfx_metal_set_ssao_enabled(false);
        if (gfx_metal_bloom_enabled()) gfx_metal_set_bloom_enabled(false);
        if (gfx_metal_god_rays_enabled()) gfx_metal_set_god_rays_enabled(false);
        gfx_metal_fallback_to_direct("offscreen color/depth/AO allocation failed");
        return false;
    }
    metal_target_width = size.width;
    metal_target_height = size.height;
    metal_target_samples = samples;
    uint64_t pixels = (uint64_t)size.width * size.height;
    uint64_t estimated_bytes = pixels * (samples > 1 ? (8u * samples + 8u) : 8u) +
                               pixels * 4u * samples;
    if (gfx_metal_ssao_enabled())
        estimated_bytes += (uint64_t)metal_ssao_width * metal_ssao_height * 2u;
    if (gfx_metal_bloom_enabled())
        estimated_bytes += (uint64_t)metal_bloom_width * metal_bloom_height * 24u;
    if (gfx_metal_shadows_enabled())
        estimated_bytes += (uint64_t)metal_shadow_texture_size * metal_shadow_texture_size * 4u;
    if (gfx_metal_god_rays_enabled())
        estimated_bytes += (uint64_t)metal_god_ray_width * metal_god_ray_height;
    @synchronized ([NSObject class]) {
        if (metal_shader64_graph) for (size_t i = 0; i < metal_shader64_graph->count; ++i)
            if (metal_shader64_graph->passes[i].target)
                estimated_bytes += (uint64_t)metal_shader64_graph->passes[i].target_width *
                                   metal_shader64_graph->passes[i].target_height * 8u;
    }
    @synchronized ([NSObject class]) { metal_performance_stats.estimated_target_bytes = estimated_bytes; }
    gfx_ios_diag_log("Metal offscreen targets ready size=%lux%lu scale=%u%% samples=%lu color=%@ resolve=%@ depth=%@", (unsigned long)size.width, (unsigned long)size.height, metal_settings.render_scale_percent, (unsigned long)samples, samples > 1 ? metal_multisample_color : metal_color_target, samples > 1 ? metal_resolve_texture : nil, depth);
    return true;
}

static bool gfx_metal_encode_final_pass(id<CAMetalDrawable> drawable,
                                        id<MTLCommandBuffer> command) {
    id<MTLTexture> source = metal_active_sample_count > 1 ? metal_resolve_texture : metal_color_target;
    bool graph_active = false;
    @synchronized ([NSObject class]) { graph_active = metal_shader64_graph != NULL; }
    if (graph_active && gfx_metal_encode_shader64_graph(drawable, command, source)) return true;
    id<MTLRenderPipelineState> present_pipeline = nil;
    BOOL custom_shader = NO;
    vector_float4 shader64_parameters = {1.0f, 0.0f, 0.0f, 0.0f};
    @synchronized ([NSObject class]) {
        custom_shader = metal_shader64_pipeline != nil;
        present_pipeline = [(custom_shader ? metal_shader64_pipeline : metal_present_pipeline) retain];
        shader64_parameters = metal_shader64_parameters;
    }
    if (!source || !present_pipeline || !metal_present_sampler) {
        gfx_ios_diag_log("Metal final pass skipped source=%@ pipeline=%@ sampler=%@", source, present_pipeline, metal_present_sampler);
        [present_pipeline release];
        return false;
    }
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) {
        gfx_ios_diag_log("Metal final pass encoder creation failed drawable=%lux%lu", (unsigned long)drawable.texture.width, (unsigned long)drawable.texture.height);
        [present_pipeline release];
        return false;
    }
    [encoder setRenderPipelineState:present_pipeline];
    [encoder setFragmentTexture:source atIndex:0];
    [encoder setFragmentSamplerState:metal_present_sampler atIndex:0];
    if (custom_shader) {
        /* API v1 parameters are four bounded floats. Reference packs use the
         * first lane for their main strength control; default is neutral. */
        [encoder setFragmentBytes:&shader64_parameters length:sizeof(shader64_parameters) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [present_pipeline release];
        return true;
    }
    [encoder setFragmentTexture:(gfx_metal_ssao_enabled() ? metal_ssao_filtered_texture : source) atIndex:1];
    [encoder setFragmentSamplerState:metal_present_sampler atIndex:1];
    [encoder setFragmentTexture:metal_depth_texture atIndex:2];
    [encoder setFragmentTexture:(gfx_metal_bloom_enabled() ? metal_bloom_output_texture : source) atIndex:3];
    /* Always bind a valid depth texture and zero the shadow range when the
     * shadow pass did not produce a map. This keeps the shared presentation
     * pipeline's resource bindings deterministic on the Classic path. */
    [encoder setFragmentTexture:(metal_shadow_texture ?: metal_depth_texture) atIndex:4];
    [encoder setFragmentTexture:(metal_god_ray_texture ?: metal_bloom_output_texture ?: source) atIndex:6];
    uint32_t debug_mode = gfx_metal_ssao_debug_mode();
    vector_uint4 present_flags = { gfx_metal_ssao_enabled() ? 1u : 0u,
                                   debug_mode == 2u ? 1u : (debug_mode == 3u ? 2u : 0u),
                                   gfx_metal_post_enabled() ? 1u : 0u,
                                   gfx_metal_fxaa_enabled() ? 1u : 0u };
    static const float fog_density[] = { 0.0007f, 0.00035f, 0.0012f, 0.00018f };
    static const float fog_start[] = { 1200.0f, 1800.0f, 600.0f, 3000.0f };
    static const vector_float4 fog_colors[] = {
        { 0.52f, 0.67f, 0.82f, 1.0f }, { 0.70f, 0.79f, 0.90f, 1.0f },
        { 0.85f, 0.42f, 0.22f, 1.0f }, { 0.40f, 0.60f, 0.76f, 1.0f }
    };
    uint32_t fog_profile = gfx_metal_fog_profile() % 4u;
    struct GfxMetalFogAdjustments fog_adjustments = gfx_metal_get_fog_adjustments();
    vector_float4 grade_params = { gfx_metal_exposure_stops(), gfx_metal_fog_enabled() ? 1.0f : 0.0f,
                                   fog_density[fog_profile] * fog_adjustments.density_scale,
                                   fog_start[fog_profile] * fog_adjustments.start_scale };
    float inverse_projection[4][4] = {{0}};
    float inverse_view[4][4] = {{0}};
    struct GfxSceneCaptureFrame capture = gfx_scene_capture_get_frame();
    bool inverse_projection_valid = capture.has_projection_matrix &&
        gfx_postprocess_invert_matrix(capture.projection_matrix, inverse_projection);
    bool inverse_view_valid = capture.has_view_matrix &&
        gfx_postprocess_invert_matrix(capture.view_matrix, inverse_view);
    if (gfx_metal_fog_enabled() && !inverse_projection_valid) {
        grade_params.y = 0.0f;
        gfx_ios_diag_log("Metal distance fog disabled for frame: inverse projection unavailable");
    }
    vector_float4 shadow_right_min = {0, 0, 0, 0};
    vector_float4 shadow_up_min = {0, 0, 0, 0};
    vector_float4 shadow_forward_min = {0, 0, 0, 0};
    vector_float4 shadow_ranges = {1, 1, 1, 0};
    if (gfx_metal_shadows_enabled() && metal_shadow_transform_valid) {
        if (inverse_view_valid && inverse_projection_valid) {
            shadow_right_min = (vector_float4){ metal_shadow_transform.right[0], metal_shadow_transform.right[1],
                metal_shadow_transform.right[2], metal_shadow_transform.minimum[0] };
            shadow_up_min = (vector_float4){ metal_shadow_transform.up[0], metal_shadow_transform.up[1],
                metal_shadow_transform.up[2], metal_shadow_transform.minimum[1] };
            shadow_forward_min = (vector_float4){ metal_shadow_transform.forward[0], metal_shadow_transform.forward[1],
                metal_shadow_transform.forward[2], metal_shadow_transform.minimum[2] };
            shadow_ranges = (vector_float4){ metal_shadow_transform.range[0], metal_shadow_transform.range[1],
                metal_shadow_transform.range[2], 0.0025f };
        } else {
            gfx_ios_diag_log("Metal shadow receiving disabled for frame: inverse camera matrices unavailable");
        }
    }
    vector_float4 fog_color = fog_colors[fog_profile];
    struct GfxMetalBloomSettings bloom_settings = gfx_metal_bloom_settings(gfx_metal_bloom_profile());
    vector_float4 bloom_params = { gfx_metal_bloom_enabled() ? 1.0f : 0.0f,
                                   bloom_settings.intensity, bloom_settings.threshold,
                                   (float)gfx_metal_tone_mapping_mode() };
    /* saturation, contrast, warm/cool red-blue bias, brightness offset */
    static const vector_float4 grades[4] = {
        { 1.00f, 1.00f, 0.00f, 0.00f }, /* neutral */
        { 1.00f, 1.02f, 0.035f, 0.00f }, /* warm */
        { 1.22f, 1.04f, 0.00f, 0.00f }, /* vivid */
        { 1.08f, 1.12f, 0.015f, -0.015f } /* cinematic */
    };
    vector_float4 grade_style = grades[gfx_metal_color_grade() % 4u];
    struct GfxMetalColorAdjustments color_adjustments = gfx_metal_effects_enabled() ?
        gfx_metal_get_color_adjustments() : gfx_metal_color_adjustments_default();
    grade_style.x *= color_adjustments.saturation;
    grade_style.y *= color_adjustments.contrast;
    grade_style.z += color_adjustments.temperature;
    grade_style.w += color_adjustments.brightness;
    vector_float4 sky_params = { (float)gfx_metal_sky_preset(), 0.0f, -2.0f, -2.0f };
    if (gfx_metal_sky_preset() != 0u) {
        struct GfxSceneCaptureFrame capture = gfx_scene_capture_get_frame();
        float sun_direction[3];
        gfx_metal_get_sun_direction(sun_direction);
        float sun_uv[2];
        if (capture.has_view_matrix && capture.has_projection_matrix &&
            gfx_postprocess_project_direction(capture.view_matrix, capture.projection_matrix,
                                              sun_direction, sun_uv)) {
            sky_params.y = 1.0f;
            sky_params.z = sun_uv[0];
            sky_params.w = sun_uv[1];
        }
    }
    [encoder setFragmentBytes:&present_flags length:sizeof(present_flags) atIndex:0];
    [encoder setFragmentBytes:&grade_params length:sizeof(grade_params) atIndex:1];
    [encoder setFragmentBytes:inverse_projection length:sizeof(inverse_projection) atIndex:2];
    [encoder setFragmentBytes:&fog_color length:sizeof(fog_color) atIndex:3];
    [encoder setFragmentBytes:&bloom_params length:sizeof(bloom_params) atIndex:4];
    [encoder setFragmentBytes:&grade_style length:sizeof(grade_style) atIndex:5];
    [encoder setFragmentBytes:&sky_params length:sizeof(sky_params) atIndex:6];
    [encoder setFragmentBytes:inverse_view length:sizeof(inverse_view) atIndex:7];
    [encoder setFragmentBytes:&shadow_right_min length:sizeof(shadow_right_min) atIndex:8];
    [encoder setFragmentBytes:&shadow_up_min length:sizeof(shadow_up_min) atIndex:9];
    [encoder setFragmentBytes:&shadow_forward_min length:sizeof(shadow_forward_min) atIndex:10];
    [encoder setFragmentBytes:&shadow_ranges length:sizeof(shadow_ranges) atIndex:11];
    float ray_params = gfx_metal_god_rays_enabled() ? 1.0f : 0.0f;
    [encoder setFragmentBytes:&ray_params length:sizeof(ray_params) atIndex:12];
    float sun_direction[3];
    gfx_metal_get_sun_direction(sun_direction);
    static const float sun_intensities[] = { 0.0f, 0.12f, 0.24f, 0.36f };
    vector_float4 sun_params = { sun_direction[0], sun_direction[1], sun_direction[2],
        inverse_projection_valid && inverse_view_valid ? sun_intensities[gfx_metal_sun_intensity()] : 0.0f };
    [encoder setFragmentBytes:&sun_params length:sizeof(sun_params) atIndex:13];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    [present_pipeline release];
    return true;
}

static bool gfx_metal_ensure_ssao_pipeline(void) {
    if (metal_ssao_pipeline) return true;
    NSString *source = [NSString stringWithUTF8String:gfx_metal_ssao_shader_source()];
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithSource:source options:nil error:&error];
    id<MTLFunction> function = library ? [library newFunctionWithName:@"sm64_ssao"] : nil;
    id<MTLFunction> denoiseFunction = library ? [library newFunctionWithName:@"sm64_ssao_denoise"] : nil;
    if (!function || !denoiseFunction) {
        gfx_ios_diag_log("Metal SSAO MSL compilation failed: %s", error.localizedDescription.UTF8String ?: "function missing");
        [function release]; [denoiseFunction release]; [library release];
        return false;
    }
    metal_ssao_pipeline = [metal_device newComputePipelineStateWithFunction:function error:&error];
    metal_ssao_denoise_pipeline = [metal_device newComputePipelineStateWithFunction:denoiseFunction error:&error];
    [function release]; [denoiseFunction release]; [library release];
    if (!metal_ssao_pipeline || !metal_ssao_denoise_pipeline) {
        gfx_ios_diag_log("Metal SSAO pipeline creation failed: %s", error.localizedDescription.UTF8String ?: "unknown");
        return false;
    }
    gfx_ios_diag_log("Metal SSAO compute pipeline ready threadsPerGroup=%lu", (unsigned long)metal_ssao_pipeline.maxTotalThreadsPerThreadgroup);
    return true;
}

static bool gfx_metal_ensure_scene_normals_pipeline(void) {
    if (metal_scene_normals_pipeline) return true;
    NSString *source = [NSString stringWithUTF8String:gfx_metal_scene_normals_shader_source()];
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithSource:source options:nil error:&error];
    id<MTLFunction> function = library ? [library newFunctionWithName:@"sm64_scene_normals"] : nil;
    if (function) metal_scene_normals_pipeline = [metal_device newComputePipelineStateWithFunction:function error:&error];
    [function release]; [library release];
    if (!metal_scene_normals_pipeline) {
        gfx_ios_diag_log("Metal scene-normal compute pipeline failed: %s", error.localizedDescription.UTF8String ?: "function or pipeline unavailable");
        return false;
    }
    gfx_ios_diag_log("Metal scene-normal reconstruction ready threadsPerGroup=%lu (view-space, half resolution)",
                     (unsigned long)metal_scene_normals_pipeline.maxTotalThreadsPerThreadgroup);
    return true;
}

static bool gfx_metal_encode_scene_normals(id<MTLCommandBuffer> command) {
    metal_scene_normals_valid_for_frame = false;
    bool requested = false;
    @synchronized ([NSObject class]) {
        if (metal_shader64_graph) for (size_t i = 0; i < metal_shader64_graph->count; ++i)
            if (metal_shader64_graph->passes[i].resource_flags & GFX_SHADER64_RESOURCE_SCENE_NORMALS) {
                requested = true; break;
            }
    }
    if (!requested) return true;
    struct GfxSceneCaptureFrame capture = gfx_scene_capture_get_frame();
    float inverse_projection[4][4];
    if (!metal_depth_texture || metal_depth_samples != 1 || !capture.has_projection_matrix ||
        !gfx_postprocess_invert_matrix(capture.projection_matrix, inverse_projection) ||
        !gfx_metal_ensure_scene_normals_pipeline()) return false;
    NSUInteger width = MAX(1u, metal_depth_width / 2u);
    NSUInteger height = MAX(1u, metal_depth_height / 2u);
    if (!metal_scene_normals_texture || metal_scene_normals_width != width || metal_scene_normals_height != height) {
        [metal_scene_normals_texture release]; metal_scene_normals_texture = nil;
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
            width:width height:height mipmapped:NO];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        metal_scene_normals_texture = [metal_device newTextureWithDescriptor:descriptor];
        if (!metal_scene_normals_texture) {
            gfx_ios_diag_log("Metal scene-normal texture allocation failed size=%lux%lu", (unsigned long)width, (unsigned long)height);
            return false;
        }
        metal_scene_normals_texture.label = @"SHADER64 view-space depth normals";
        metal_scene_normals_width = width; metal_scene_normals_height = height;
    }
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder) return false;
    [encoder setComputePipelineState:metal_scene_normals_pipeline];
    [encoder setTexture:metal_depth_texture atIndex:0];
    [encoder setTexture:metal_scene_normals_texture atIndex:1];
    [encoder setBytes:inverse_projection length:sizeof(inverse_projection) atIndex:0];
    NSUInteger thread_width = metal_scene_normals_pipeline.threadExecutionWidth;
    NSUInteger thread_height = MAX(1u, metal_scene_normals_pipeline.maxTotalThreadsPerThreadgroup / thread_width);
    [encoder dispatchThreads:MTLSizeMake(width, height, 1)
        threadsPerThreadgroup:MTLSizeMake(thread_width, thread_height, 1)];
    [encoder endEncoding];
    metal_scene_normals_valid_for_frame = true;
    return true;
}

static bool gfx_metal_encode_ssao(id<MTLCommandBuffer> command) {
    if (!gfx_metal_ssao_enabled()) return true;
    struct GfxSceneCaptureFrame capture = gfx_scene_capture_get_frame();
    float inverse_projection[4][4];
    if (!capture.has_projection_matrix ||
        !gfx_postprocess_invert_matrix(capture.projection_matrix, inverse_projection) ||
        !metal_depth_texture || !metal_ssao_texture || !gfx_metal_ensure_ssao_pipeline()) {
        gfx_ios_diag_log("Metal SSAO skipped: projection, depth, output, or compute pipeline unavailable");
        return false;
    }
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder) return false;
    static const vector_float4 quality_parameters[] = {
        { 2.0f, 0.035f, 0.85f, 80.0f },
        { 2.0f, 0.025f, 1.15f, 120.0f },
        { 3.0f, 0.018f, 1.35f, 170.0f }
    };
    static const uint32_t quality_samples[] = { 4u, 8u, 8u };
    uint32_t quality = gfx_metal_ssao_quality();
    vector_float4 parameters = quality_parameters[quality];
    struct GfxMetalAOAdjustments adjustments = gfx_metal_get_ao_adjustments();
    parameters.x *= adjustments.radius_scale;
    parameters.y = MAX(0.0f, parameters.y + adjustments.bias_offset);
    parameters.z *= adjustments.intensity_scale;
    uint32_t sample_count = quality_samples[quality];
    [encoder setComputePipelineState:metal_ssao_pipeline];
    [encoder setTexture:metal_depth_texture atIndex:0];
    [encoder setTexture:metal_ssao_texture atIndex:1];
    [encoder setBytes:inverse_projection length:sizeof(inverse_projection) atIndex:0];
    [encoder setBytes:&parameters length:sizeof(parameters) atIndex:1];
    [encoder setBytes:&sample_count length:sizeof(sample_count) atIndex:2];
    NSUInteger width = metal_ssao_width, height = metal_ssao_height;
    NSUInteger thread_width = metal_ssao_pipeline.threadExecutionWidth;
    NSUInteger thread_height = MAX(1u, metal_ssao_pipeline.maxTotalThreadsPerThreadgroup / thread_width);
    [encoder dispatchThreads:MTLSizeMake(width, height, 1)
        threadsPerThreadgroup:MTLSizeMake(thread_width, thread_height, 1)];
    [encoder endEncoding];
    encoder = [command computeCommandEncoder];
    if (!encoder) return false;
    [encoder setComputePipelineState:metal_ssao_denoise_pipeline];
    [encoder setTexture:metal_depth_texture atIndex:0];
    [encoder setTexture:metal_ssao_texture atIndex:1];
    [encoder setTexture:metal_ssao_filtered_texture atIndex:2];
    NSUInteger blur_width = metal_ssao_denoise_pipeline.threadExecutionWidth;
    NSUInteger blur_height = MAX(1u, metal_ssao_denoise_pipeline.maxTotalThreadsPerThreadgroup / blur_width);
    [encoder dispatchThreads:MTLSizeMake(width, height, 1)
        threadsPerThreadgroup:MTLSizeMake(blur_width, blur_height, 1)];
    [encoder endEncoding];
    @synchronized ([NSObject class]) {
        metal_performance_stats.ssao_pixels = width * height;
    }
    return true;
}

static bool gfx_metal_ensure_god_ray_pipeline(void) {
    if (metal_god_ray_pipeline) return true;
    NSString *source = [NSString stringWithUTF8String:gfx_metal_god_ray_shader_source()];
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithSource:source options:nil error:&error];
    id<MTLFunction> function = library ? [library newFunctionWithName:@"sm64_god_rays"] : nil;
    if (!function) {
        gfx_ios_diag_log("Metal god-ray MSL compilation failed: %s", error.localizedDescription.UTF8String ?: "function missing");
        [library release];
        return false;
    }
    metal_god_ray_pipeline = [metal_device newComputePipelineStateWithFunction:function error:&error];
    [function release]; [library release];
    if (!metal_god_ray_pipeline) {
        gfx_ios_diag_log("Metal god-ray compute pipeline creation failed: %s", error.localizedDescription.UTF8String ?: "unknown");
        return false;
    }
    return true;
}

static bool gfx_metal_encode_god_rays(id<MTLCommandBuffer> command) {
    if (!gfx_metal_god_rays_enabled()) return true;
    if (!metal_depth_texture || !metal_god_ray_texture || !gfx_metal_ensure_god_ray_pipeline()) return false;
    struct GfxSceneCaptureFrame capture = gfx_scene_capture_get_frame();
    float sun_uv[2] = { -2.0f, -2.0f };
    float sun_direction[3];
    gfx_metal_get_sun_direction(sun_direction);
    if (capture.has_view_matrix && capture.has_projection_matrix)
        (void)gfx_postprocess_project_direction(capture.view_matrix, capture.projection_matrix,
                                                sun_direction, sun_uv);
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder) return false;
    static const uint32_t quality_samples[] = { 4u, 8u, 12u };
    uint32_t quality = gfx_metal_god_ray_quality();
    float strength = (quality == 0 ? 0.35f : (quality == 1 ? 0.55f : 0.75f)) *
                     gfx_metal_god_ray_intensity();
    uint32_t sample_count = quality_samples[quality];
    [encoder setComputePipelineState:metal_god_ray_pipeline];
    [encoder setTexture:metal_depth_texture atIndex:0];
    [encoder setTexture:metal_god_ray_texture atIndex:1];
    [encoder setBytes:sun_uv length:sizeof(sun_uv) atIndex:0];
    [encoder setBytes:&strength length:sizeof(strength) atIndex:1];
    [encoder setBytes:&sample_count length:sizeof(sample_count) atIndex:2];
    NSUInteger width = metal_god_ray_width, height = metal_god_ray_height;
    NSUInteger tw = metal_god_ray_pipeline.threadExecutionWidth;
    NSUInteger th = MAX(1u, metal_god_ray_pipeline.maxTotalThreadsPerThreadgroup / tw);
    [encoder dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
    [encoder endEncoding];
    @synchronized ([NSObject class]) { metal_performance_stats.god_ray_pixels = width * height; }
    return true;
}

static bool gfx_metal_ensure_shadow_pipeline(void) {
    if (metal_shadow_pipeline && metal_shadow_depth_state && metal_shadow_vertex_buffer) return true;
    NSString *source = [NSString stringWithUTF8String:gfx_metal_shadow_shader_source()];
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithSource:source options:nil error:&error];
    id<MTLFunction> vertex = library ? [library newFunctionWithName:@"sm64_shadow_vertex"] : nil;
    MTLRenderPipelineDescriptor *descriptor = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    descriptor.vertexFunction = vertex;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    descriptor.label = @"SM64 directional shadow depth pipeline";
    metal_shadow_pipeline = [metal_device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    [vertex release]; [library release];
    if (!metal_shadow_pipeline) {
        gfx_ios_diag_log("Metal shadow depth pipeline creation failed: %s", error.localizedDescription.UTF8String ?: "unknown");
        return false;
    }
    MTLDepthStencilDescriptor *depth = [[[MTLDepthStencilDescriptor alloc] init] autorelease];
    depth.depthCompareFunction = MTLCompareFunctionLess;
    depth.depthWriteEnabled = YES;
    metal_shadow_depth_state = [metal_device newDepthStencilStateWithDescriptor:depth];
    if (!metal_shadow_vertex_buffer) {
        metal_shadow_vertex_buffer = [metal_device newBufferWithLength:METAL_SHADOW_VERTEX_LIMIT * sizeof(vector_float4)
                                                               options:MTLResourceStorageModeShared];
        metal_shadow_vertex_buffer.label = @"SM64 bounded shadow vertices";
    }
    if (!metal_shadow_world_positions)
        metal_shadow_world_positions = calloc(METAL_SHADOW_VERTEX_LIMIT, sizeof(*metal_shadow_world_positions));
    if (!metal_shadow_depth_state || !metal_shadow_vertex_buffer || !metal_shadow_world_positions) {
        gfx_ios_diag_log("Metal shadow depth resources unavailable (state=%d buffer=%d cpu=%d)",
                         metal_shadow_depth_state != nil, metal_shadow_vertex_buffer != nil,
                         metal_shadow_world_positions != NULL);
        return false;
    }
    gfx_ios_diag_log("Metal shadow depth pipeline ready vertexLimit=%u", METAL_SHADOW_VERTEX_LIMIT);
    return true;
}

static bool gfx_metal_encode_shadows(id<MTLCommandBuffer> command) {
    if (!gfx_metal_shadows_enabled()) return true;
    bool previous_transform_valid = metal_shadow_transform_valid;
    struct GfxSceneCaptureFrame capture = gfx_scene_capture_get_frame();
    if (!capture.triangles || capture.triangle_count == 0 || !metal_shadow_texture ||
        !gfx_metal_ensure_shadow_pipeline()) {
        metal_shadow_transform_valid = false;
        metal_shadow_cache_valid = false;
        return false;
    }
    size_t limit = capture.triangle_count < 8192u ? capture.triangle_count : 8192u;
    size_t vertex_count = 0;
    for (size_t t = 0; t < limit; ++t) {
        const struct GfxSceneCaptureTriangle *triangle = &capture.triangles[t];
        if (!triangle->has_world_transform) continue;
        if (vertex_count + 3u > METAL_SHADOW_VERTEX_LIMIT) break;
        for (size_t v = 0; v < 3; ++v) {
            const float *local = triangle->vertices[v].position;
            if (!gfx_shadow_map_transform_point(triangle->object_transform, local,
                                                metal_shadow_world_positions[vertex_count]))
                { metal_shadow_transform_valid = false; metal_shadow_cache_valid = false; return false; }
            ++vertex_count;
        }
    }
    if (vertex_count < 3u) {
        metal_shadow_transform_valid = false;
        metal_shadow_cache_valid = false;
        return false;
    }
    float sun_ray[3];
    gfx_metal_get_sun_direction(sun_ray);
    for (int axis = 0; axis < 3; ++axis) sun_ray[axis] = -sun_ray[axis];
    uint64_t geometry_key = gfx_shadow_map_geometry_key((const float (*)[3])metal_shadow_world_positions,
        vertex_count, sun_ray, (uint32_t)metal_shadow_texture_size);
    if (geometry_key != 0 && metal_shadow_cache_valid && previous_transform_valid &&
        geometry_key == metal_shadow_cached_geometry_key &&
        vertex_count == metal_shadow_cached_vertex_count) {
        @synchronized ([NSObject class]) { metal_performance_stats.shadow_triangles = vertex_count / 3u; }
        return true;
    }
    metal_shadow_transform_valid = false;
    if (!gfx_shadow_map_fit((const float (*)[3])metal_shadow_world_positions, vertex_count,
                            sun_ray, (uint32_t)metal_shadow_texture_size, &metal_shadow_transform)) {
        metal_shadow_cache_valid = false;
        return false;
    }
    vector_float4 *clip_positions = metal_shadow_vertex_buffer.contents;
    for (size_t i = 0; i < vertex_count; ++i) {
        float clip[3], uv_depth[3];
        if (!gfx_shadow_map_project(&metal_shadow_transform, metal_shadow_world_positions[i], clip, uv_depth)) {
            metal_shadow_cache_valid = false;
            return false;
        }
        clip_positions[i] = (vector_float4){ clip[0], clip[1], clip[2], 1.0f };
    }
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.depthAttachment.texture = metal_shadow_texture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = 1.0;
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) { metal_shadow_cache_valid = false; return false; }
    [encoder setRenderPipelineState:metal_shadow_pipeline];
    [encoder setDepthStencilState:metal_shadow_depth_state];
    [encoder setViewport:(MTLViewport){ 0.0, 0.0, (double)metal_shadow_texture_size,
                                        (double)metal_shadow_texture_size, 0.0, 1.0 }];
    [encoder setVertexBuffer:metal_shadow_vertex_buffer offset:0 atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertex_count];
    [encoder endEncoding];
    metal_shadow_transform_valid = true;
    metal_shadow_cache_valid = geometry_key != 0;
    metal_shadow_cached_geometry_key = geometry_key;
    metal_shadow_cached_vertex_count = vertex_count;
    @synchronized ([NSObject class]) { metal_performance_stats.shadow_triangles = vertex_count / 3u; }
    return true;
}

static bool gfx_metal_ensure_bloom_pipeline(void) {
    if (metal_bloom_extract_pipeline && metal_bloom_blur_pipeline) return true;
    NSString *source = [NSString stringWithUTF8String:gfx_metal_bloom_shader_source()];
    NSError *error = nil;
    id<MTLLibrary> library = [metal_device newLibraryWithSource:source options:nil error:&error];
    id<MTLFunction> extract = library ? [library newFunctionWithName:@"sm64_bloom_extract"] : nil;
    id<MTLFunction> blur = library ? [library newFunctionWithName:@"sm64_bloom_blur"] : nil;
    if (!extract || !blur) {
        gfx_ios_diag_log("Metal bloom MSL compilation failed: %s", error.localizedDescription.UTF8String ?: "function missing");
        [extract release]; [blur release]; [library release];
        return false;
    }
    metal_bloom_extract_pipeline = [metal_device newComputePipelineStateWithFunction:extract error:&error];
    metal_bloom_blur_pipeline = [metal_device newComputePipelineStateWithFunction:blur error:&error];
    [extract release]; [blur release]; [library release];
    if (!metal_bloom_extract_pipeline || !metal_bloom_blur_pipeline) {
        gfx_ios_diag_log("Metal bloom pipeline creation failed: %s", error.localizedDescription.UTF8String ?: "unknown");
        return false;
    }
    gfx_ios_diag_log("Metal bloom pipelines ready at quarter render resolution");
    return true;
}

static void gfx_metal_dispatch_bloom(id<MTLComputeCommandEncoder> encoder,
                                     id<MTLComputePipelineState> pipeline,
                                     id<MTLTexture> input, id<MTLTexture> output,
                                     const void *parameters, size_t parameter_size) {
    [encoder setComputePipelineState:pipeline];
    [encoder setTexture:input atIndex:0];
    [encoder setTexture:output atIndex:1];
    if (parameters && parameter_size) [encoder setBytes:parameters length:parameter_size atIndex:0];
    NSUInteger tw = pipeline.threadExecutionWidth;
    NSUInteger th = MAX(1u, pipeline.maxTotalThreadsPerThreadgroup / tw);
    [encoder dispatchThreads:MTLSizeMake(metal_bloom_width, metal_bloom_height, 1)
        threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
}

static bool gfx_metal_encode_bloom(id<MTLCommandBuffer> command) {
    if (!gfx_metal_bloom_enabled()) return true;
    id<MTLTexture> source = metal_active_sample_count > 1 ? metal_resolve_texture : metal_color_target;
    if (!source || !metal_bloom_extract_texture || !metal_bloom_temp_texture || !metal_bloom_output_texture ||
        !gfx_metal_ensure_bloom_pipeline()) return false;
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder) return false;
    struct GfxMetalBloomSettings bloom_settings = gfx_metal_bloom_settings(gfx_metal_bloom_profile());
    float threshold = bloom_settings.threshold;
    gfx_metal_dispatch_bloom(encoder, metal_bloom_extract_pipeline, source, metal_bloom_extract_texture,
                             &threshold, sizeof(threshold));
    uint32_t axis = 0;
    gfx_metal_dispatch_bloom(encoder, metal_bloom_blur_pipeline, metal_bloom_extract_texture, metal_bloom_temp_texture,
                             &axis, sizeof(axis));
    axis = 1;
    gfx_metal_dispatch_bloom(encoder, metal_bloom_blur_pipeline, metal_bloom_temp_texture, metal_bloom_output_texture,
                             &axis, sizeof(axis));
    [encoder endEncoding];
    @synchronized ([NSObject class]) {
        metal_performance_stats.bloom_pixels = (uint64_t)metal_bloom_width * metal_bloom_height * 3u;
    }
    return true;
}

static void gfx_metal_init(void) {
    gfx_ios_diag_log("Metal graphics API init entered backend=RAPI_METAL device=%@ view=%@ drawable=%.0fx%.0f", metal_device.name, metal_view, metal_view.drawableSize.width, metal_view.drawableSize.height);
    if (!metal_device) metal_device = MTLCreateSystemDefaultDevice();
    if (!metal_device) { gfx_ios_diag_log("Metal graphics API init failed: no Metal device available"); NSLog(@"Metal initialization failed: no Metal device available"); return; }
    if (!metal_queue) metal_queue = [metal_device newCommandQueue];
    if (!metal_queue) { gfx_ios_diag_log("Metal graphics API init failed: command queue unavailable"); NSLog(@"Metal initialization failed: could not create command queue"); return; }
    if (!metal_view) { gfx_ios_diag_log("Metal graphics API init warning: no MTKView was attached before renderer initialization"); NSLog(@"Metal view was not attached before renderer initialization"); }
    gfx_metal_refresh_capabilities();
    metal_settings = gfx_metal_read_persisted_settings();
    metal_performance_stats.preset = metal_settings.preset;
    metal_performance_stats.render_scale_percent = metal_settings.render_scale_percent;
    metal_performance_stats.sample_count = metal_settings.sample_count;
    metal_performance_stats.anisotropy = metal_settings.anisotropy;
    gfx_metal_persist_settings(metal_settings);
    gfx_ios_diag_log("Metal startup graphics settings preset=%s scale=%u%% samples=%ux anisotropy=%u hdTextures=%d", gfx_metal_preset_name(metal_settings.preset), metal_settings.render_scale_percent, metal_settings.sample_count, metal_settings.anisotropy, metal_settings.hd_textures);
    texture_count = 0;
    for (int i = 0; i < 2; ++i) {
        [metal_samplers[i] release];
        MTLSamplerDescriptor *desc = [[[MTLSamplerDescriptor alloc] init] autorelease];
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.magFilter = MTLSamplerMinMagFilterNearest;
        desc.sAddressMode = MTLSamplerAddressModeRepeat;
        desc.tAddressMode = MTLSamplerAddressModeRepeat;
        metal_samplers[i] = [metal_device newSamplerStateWithDescriptor:desc];
        sampler_anisotropy[i] = 1;
        if (!metal_samplers[i]) gfx_ios_diag_log("Metal default sampler creation failed tile=%d", i);
    }
    if (gfx_metal_settings_use_offscreen(metal_settings) && !gfx_metal_create_present_pipeline())
        gfx_metal_fallback_to_direct("startup final-pass pipeline failed");
    gfx_ios_diag_log("Metal graphics API init completed backend=RAPI_METAL device=%s commandQueue=%@ view=%@", metal_device.name.UTF8String ?: "unknown", metal_queue, metal_view);
}
static void gfx_metal_on_resize(void) {
    if (!metal_view) return;
    log_next_draw_rects = true;
    UIView *host_view = metal_view.superview;
    if (host_view) {
        [host_view setNeedsLayout];
        [host_view layoutIfNeeded];
        // UIKit can restore the SDL window and its child view at different
        // points in the foreground transition. Reconcile the game surface to
        // the live host bounds before the first resumed draw.
        if (!CGRectEqualToRect(metal_view.frame, host_view.bounds))
            metal_view.frame = host_view.bounds;
        CGFloat scale = metal_view.window.windowScene.screen.scale;
        if (scale <= 0.0) scale = UIScreen.mainScreen.scale;
        if (scale > 0.0 && metal_view.contentScaleFactor != scale)
            metal_view.contentScaleFactor = scale;
    }
    [metal_view setNeedsLayout];
    [metal_view layoutIfNeeded];
    CGSize bounds = metal_view.bounds.size;
    CGFloat scale = metal_view.contentScaleFactor;
    CGSize expected_drawable = CGSizeMake((NSUInteger)(bounds.width * scale + 0.5),
                                          (NSUInteger)(bounds.height * scale + 0.5));
    CGSize actual_drawable = metal_view.drawableSize;
    if (expected_drawable.width > 0.0 && expected_drawable.height > 0.0 &&
        (actual_drawable.width != expected_drawable.width || actual_drawable.height != expected_drawable.height)) {
        gfx_ios_diag_log("Metal drawable resize reconciliation old=%.0fx%.0f expected=%.0fx%.0f bounds=%.0fx%.0f scale=%.2f", actual_drawable.width, actual_drawable.height, expected_drawable.width, expected_drawable.height, bounds.width, bounds.height, scale);
        metal_view.drawableSize = expected_drawable;
        actual_drawable = metal_view.drawableSize;
    }
    gfx_ios_diag_log("Game MTKView resize/layout hostBounds=%.0fx%.0f frame=(%.0f,%.0f %.0fx%.0f) bounds=%.0fx%.0f drawable=%.0fx%.0f expected=%.0fx%.0f scale=%.2f windowBounds=%.0fx%.0f scene=%@", host_view.bounds.size.width, host_view.bounds.size.height, metal_view.frame.origin.x, metal_view.frame.origin.y, metal_view.frame.size.width, metal_view.frame.size.height, bounds.width, bounds.height, actual_drawable.width, actual_drawable.height, expected_drawable.width, expected_drawable.height, scale, metal_view.window.bounds.size.width, metal_view.window.bounds.size.height, metal_view.window.windowScene.session.persistentIdentifier);
}

static bool gfx_metal_encode_scene_composite(void) {
    if (!gfx_metal_encode_bloom(metal_command)) {
        gfx_metal_set_bloom_enabled(false);
        gfx_ios_diag_log("Metal bloom disabled after texture or compute-pipeline failure");
    }
    if (!gfx_metal_encode_ssao(metal_command)) {
        gfx_metal_set_ssao_enabled(false);
        gfx_ios_diag_log("Metal SSAO disabled after frame resources or projection became unavailable");
    }
    if (!gfx_metal_encode_god_rays(metal_command)) {
        gfx_metal_set_god_rays_enabled(false);
        gfx_ios_diag_log("Metal screen-space light shafts disabled after depth or compute-pipeline failure");
    }
    if (!gfx_metal_encode_shadows(metal_command)) {
        metal_shadow_transform_valid = false;
        @synchronized ([NSObject class]) { metal_performance_stats.shadow_triangles = 0; }
        if (frame_count <= 3 || frame_count % 120 == 0)
            gfx_ios_diag_log("Metal directional shadow map skipped: no bounded world-space caster set or pass resources unavailable");
    }
    if (!gfx_metal_encode_scene_normals(metal_command)) {
        static uint32_t normal_unavailable_logs;
        if (normal_unavailable_logs++ < 8)
            gfx_ios_diag_log("SHADER64 sceneNormals unavailable; external graph will fall back to the native composite");
    }
    if (metal_frame_uses_offscreen && !gfx_metal_encode_final_pass(metal_drawable, metal_command)) {
        metal_offscreen_runtime_failed = true;
        return false;
    }
    return true;
}

static bool gfx_metal_ensure_ui_depth(NSUInteger width, NSUInteger height) {
    if (metal_ui_depth_texture && metal_ui_depth_width == width && metal_ui_depth_height == height)
        return true;
    uint64_t previous_bytes = metal_ui_depth_texture ? (uint64_t)metal_ui_depth_width * metal_ui_depth_height * 4u : 0u;
    [metal_ui_depth_texture release];
    metal_ui_depth_texture = nil;
    metal_ui_depth_width = metal_ui_depth_height = 0;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                           width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget;
    metal_ui_depth_texture = [metal_device newTextureWithDescriptor:descriptor];
    if (!metal_ui_depth_texture) return false;
    metal_ui_depth_texture.label = @"SM64 HUD depth";
    metal_ui_depth_width = width;
    metal_ui_depth_height = height;
    @synchronized ([NSObject class]) {
        if (metal_performance_stats.estimated_target_bytes >= previous_bytes)
            metal_performance_stats.estimated_target_bytes -= previous_bytes;
        metal_performance_stats.estimated_target_bytes += (uint64_t)width * height * 4u;
    }
    return true;
}

static void gfx_metal_begin_ui_overlay(void) {
    if (!metal_frame_uses_offscreen || metal_scene_postprocessed_before_ui || !metal_encoder ||
        !metal_command || !metal_drawable) return;
    NSUInteger width = metal_drawable.texture.width;
    NSUInteger height = metal_drawable.texture.height;
    /* If the UI depth target cannot be created, leave the original encoder
     * active. The normal end-of-frame composite then handles the whole list. */
    if (!gfx_metal_ensure_ui_depth(width, height)) {
        gfx_ios_diag_log("Metal HUD split skipped: drawable-sized depth target unavailable");
        return;
    }
    [metal_encoder endEncoding];
    metal_encoder = nil;
    bool composited = gfx_metal_encode_scene_composite();
    metal_scene_postprocessed_before_ui = true;
    metal_frame_uses_offscreen = false;
    metal_active_sample_count = 1;
    metal_active_render_width = width;
    metal_active_render_height = height;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = metal_drawable.texture;
    pass.colorAttachments[0].loadAction = composited ? MTLLoadActionLoad : MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    pass.depthAttachment.texture = metal_ui_depth_texture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
    metal_encoder = [metal_command renderCommandEncoderWithDescriptor:pass];
    if (!metal_encoder) {
        @synchronized ([NSObject class]) { metal_offscreen_runtime_failed = true; }
        gfx_ios_diag_log("Metal HUD overlay encoder unavailable after scene composite");
        return;
    }
    if (!composited) {
        gfx_ios_diag_log("Metal HUD overlay using direct fallback clear after scene composite failure");
    } else if (frame_count <= 3 || frame_count % 120 == 0) {
        gfx_ios_diag_log("Metal scene composited before HUD; UI uses drawable-resolution pass with isolated depth");
    }
}

static void gfx_metal_start_frame(void) {
    if (!metal_view || !metal_queue) {
        if (drawable_unavailable_count++ < 4) gfx_ios_diag_log("Metal frame start unavailable view=%@ queue=%@", metal_view, metal_queue);
        return;
    }
    if (!gfx_metal_scene_can_render()) {
        if (inactive_scene_frame_count++ < 3)
            gfx_ios_diag_log("Metal frame withheld while UIKit scene is inactive sceneActive=%d appState=%ld sceneState=%ld", metal_scene_active, (long)UIApplication.sharedApplication.applicationState, (long)metal_view.window.windowScene.activationState);
        return;
    }
    inactive_scene_frame_count = 0;
    if (!gfx_metal_surface_is_ready()) {
        uint32_t unavailable = ++drawable_unavailable_count;
        if (unavailable <= 5 || unavailable % 60 == 0)
            gfx_ios_diag_log("Metal game frame blocked until SDL scene/view/drawable is ready count=%u window=%@ scene=%@ frame=%.0fx%.0f drawable=%.0fx%.0f appState=%ld", unavailable, metal_view.window, metal_view.window.windowScene.session.persistentIdentifier, metal_view.frame.size.width, metal_view.frame.size.height, metal_view.drawableSize.width, metal_view.drawableSize.height, (long)UIApplication.sharedApplication.applicationState);
        return;
    }
    // The game owns its frame loop, so it does not run inside MTKView's
    // draw(_:) / drawInMTKView: callback. MTKView.currentDrawable is scoped
    // to that callback and is not advanced by this manual loop. Acquire each
    // frame from the backing layer instead of reusing MTKView's cached one.
    CAMetalLayer *metal_layer = (CAMetalLayer *)metal_view.layer;
    metal_drawable = [metal_layer nextDrawable];
    MTLRenderPassDescriptor *pass = nil;
    metal_frame_uses_offscreen = metal_drawable && gfx_metal_ensure_offscreen_targets(metal_drawable);
    metal_scene_postprocessed_before_ui = false;
    if (metal_drawable) {
        NSUInteger width = metal_frame_uses_offscreen ? metal_target_width : metal_drawable.texture.width;
        NSUInteger height = metal_frame_uses_offscreen ? metal_target_height : metal_drawable.texture.height;
        NSUInteger samples = metal_frame_uses_offscreen ? metal_target_samples : 1;
        id<MTLTexture> depth_texture = gfx_metal_depth_for_size(width, height, samples);
        id<MTLTexture> color_texture = metal_frame_uses_offscreen ?
            (samples > 1 ? metal_multisample_color : metal_color_target) : metal_drawable.texture;
        if (depth_texture && color_texture) {
            pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = color_texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = samples > 1 ? MTLStoreActionMultisampleResolve : MTLStoreActionStore;
            if (samples > 1) pass.colorAttachments[0].resolveTexture = metal_resolve_texture;
            pass.depthAttachment.texture = depth_texture;
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            pass.depthAttachment.storeAction = metal_frame_uses_offscreen ?
                MTLStoreActionStore : MTLStoreActionDontCare;
            pass.depthAttachment.clearDepth = 1.0;
            metal_active_sample_count = samples;
            metal_active_render_width = width;
            metal_active_render_height = height;
        }
    }
    if (!metal_drawable || !pass) {
        uint32_t unavailable = ++drawable_unavailable_count;
        if (unavailable <= 5 || unavailable % 60 == 0)
            gfx_ios_diag_log("Metal frame drawable unavailable count=%u pass=%s drawable=%s viewFrame=(%.0f,%.0f %.0fx%.0f) drawableSize=%.0fx%.0f attachedWindow=%@ scene=%@ windowHidden=%d viewHidden=%d appState=%ld", unavailable, pass ? "yes" : "nil", metal_drawable ? "yes" : "nil", metal_view.frame.origin.x, metal_view.frame.origin.y, metal_view.frame.size.width, metal_view.frame.size.height, metal_view.drawableSize.width, metal_view.drawableSize.height, metal_view.window, metal_view.window.windowScene.session.persistentIdentifier, metal_view.window.hidden, metal_view.hidden, (long)UIApplication.sharedApplication.applicationState);
        metal_drawable = nil;
        return;
    }
    uintptr_t drawable_identity = (uintptr_t)(void *)metal_drawable;
    uintptr_t drawable_texture_identity = (uintptr_t)(void *)metal_drawable.texture;
    uint64_t acquisition_index;
    bool repeated_immediately;
    @synchronized ([NSObject class]) {
        acquisition_index = ++acquired_drawable_count;
        /* CAMetalDrawable wrapper objects may reuse the same Objective-C
         * address after release. Texture identity distinguishes the actual
         * drawable backing resource from wrapper allocator reuse. */
        repeated_immediately = acquisition_index > 1 &&
                               drawable_texture_identity == last_acquired_drawable_texture_identity;
        if (repeated_immediately)
            ++repeated_drawable_texture_count;
        last_acquired_drawable_texture_identity = drawable_texture_identity;
    }
    if (acquisition_index <= 5 || acquisition_index % 120 == 0 || repeated_immediately) {
        gfx_ios_diag_log("Metal CAMetalLayer drawable acquired count=%llu frame=%u drawableObject=%p drawableTexture=%p size=%lux%lu repeatedTextureImmediately=%d repeatedTextureTotal=%llu mainThread=%d", (unsigned long long)acquisition_index, frame_count + 1, (void *)drawable_identity, (void *)drawable_texture_identity, (unsigned long)metal_drawable.texture.width, (unsigned long)metal_drawable.texture.height, repeated_immediately, (unsigned long long)repeated_drawable_texture_count, NSThread.isMainThread);
    }
    metal_command = [metal_queue commandBuffer];
    if (!metal_command) {
        gfx_ios_diag_log("Metal frame %u could not create command buffer queue=%@ drawable=%@", frame_count + 1, metal_queue, metal_drawable);
        NSLog(@"Metal frame %u could not create a command buffer", frame_count + 1);
        metal_drawable = nil;
        return;
    }
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
    metal_encoder = [metal_command renderCommandEncoderWithDescriptor:pass];
    if (!metal_encoder) {
        gfx_ios_diag_log("Metal frame %u could not create render encoder pass=%@ drawable=%@", frame_count + 1, pass, metal_drawable);
        NSLog(@"Metal frame %u could not create a render encoder", frame_count + 1);
        metal_command = nil;
        metal_drawable = nil;
        return;
    }
    ++frame_count;
    metal_frame_draw_calls = 0;
    metal_frame_triangles = 0;
    double frame_start_time = CACurrentMediaTime();
    @synchronized ([NSObject class]) {
        if (metal_last_frame_start_time > 0.0) {
            double interval = frame_start_time - metal_last_frame_start_time;
            if (interval > 0.0) {
                double fps = 1.0 / interval;
                metal_performance_stats.estimated_fps = metal_performance_stats.estimated_fps > 0.0 ?
                    metal_performance_stats.estimated_fps * 0.85 + fps * 0.15 : fps;
            }
        }
        metal_last_frame_start_time = frame_start_time;
        metal_performance_stats.render_width = (uint32_t)metal_active_render_width;
        metal_performance_stats.render_height = (uint32_t)metal_active_render_height;
        metal_performance_stats.draw_calls = 0;
        metal_performance_stats.triangles = 0;
        metal_performance_stats.preset = metal_settings.preset;
        metal_performance_stats.render_scale_percent = metal_settings.render_scale_percent;
        metal_performance_stats.sample_count = (uint32_t)metal_active_sample_count;
        metal_performance_stats.anisotropy = metal_settings.anisotropy;
    }
    if (frame_count <= 3 || frame_count % 120 == 0)
        gfx_ios_diag_log("Metal game frame encoding started frame=%u drawable=%.0fx%.0f render=%lux%lu samples=%lu offscreen=%d viewVisible=%d alpha=%.2f scene=%@", frame_count, metal_view.drawableSize.width, metal_view.drawableSize.height, (unsigned long)metal_active_render_width, (unsigned long)metal_active_render_height, (unsigned long)metal_active_sample_count, metal_frame_uses_offscreen, !metal_view.hidden && metal_view.window && !metal_view.window.hidden, metal_view.alpha, metal_view.window.windowScene.session.persistentIdentifier);
    has_viewport = has_scissor = false;
}
static void gfx_metal_end_frame(void) {
    if (metal_encoder) {
        [metal_encoder endEncoding];
        metal_encoder = nil;
    } else if (!metal_scene_postprocessed_before_ui) {
        return;
    }
    if (!metal_scene_postprocessed_before_ui)
        (void)gfx_metal_encode_scene_composite();
    @synchronized ([NSObject class]) {
        metal_performance_stats.draw_calls = metal_frame_draw_calls;
        metal_performance_stats.triangles = metal_frame_triangles;
    }
    id<CAMetalDrawable> submitted_drawable = metal_drawable;
    double drawable_width = metal_view.drawableSize.width;
    double drawable_height = metal_view.drawableSize.height;
    uint32_t submitted_frame = frame_count;
#ifdef IOS_SCENE_DIAGNOSTIC
    if (submitted_frame <= 3 || submitted_frame % 120 == 0) {
        gfx_ios_diag_log("Metal frame encoded frame=%u drawCalls=%llu triangles=%llu renderTarget=%lux%lu",
                         submitted_frame, (unsigned long long)metal_frame_draw_calls,
                         (unsigned long long)metal_frame_triangles,
                         (unsigned long)metal_active_render_width,
                         (unsigned long)metal_active_render_height);
    }
#endif
    uintptr_t submitted_drawable_identity = (uintptr_t)(void *)submitted_drawable;
    uintptr_t submitted_texture_identity = (uintptr_t)(void *)submitted_drawable.texture;
#if !TARGET_OS_SIMULATOR
    [submitted_drawable addPresentedHandler:^(id<MTLDrawable> presented_drawable) {
        id<CAMetalDrawable> callback_drawable = (id<CAMetalDrawable>)presented_drawable;
        double presented_time = presented_drawable.presentedTime;
        uint64_t callback_index;
        uint64_t displayed_index = 0;
        @synchronized ([NSObject class]) {
            callback_index = ++presentation_callback_count;
            if (presented_time > 0.0)
                displayed_index = ++displayed_drawable_count;
        }
        if (callback_index <= 10 || callback_index % 60 == 0 || (presented_time > 0.0 && displayed_index <= 10)) {
            gfx_ios_diag_log("Metal CoreAnimation presentation callback count=%llu displayedCount=%llu submittedFrame=%u submittedDrawable=%p callbackDrawable=%p identityMatches=%d submittedTexture=%p callbackTexture=%p textureIdentityMatches=%d presentedTime=%.6f wasDisplayed=%d size=%.0fx%.0f", (unsigned long long)callback_index, (unsigned long long)displayed_index, submitted_frame, (void *)submitted_drawable_identity, (void *)presented_drawable, (uintptr_t)(void *)presented_drawable == submitted_drawable_identity, (void *)submitted_texture_identity, (void *)callback_drawable.texture, (uintptr_t)(void *)callback_drawable.texture == submitted_texture_identity, presented_time, presented_time > 0.0, drawable_width, drawable_height);
        }
        if (presented_time > 0.0 && !logged_first_presented_frame) {
            logged_first_presented_frame = true;
            gfx_ios_diag_log("First SM64 game drawable confirmed displayed by CoreAnimation presentedTime=%.6f drawable=%p frame=%u size=%.0fx%.0f", presented_time, (void *)presented_drawable, submitted_frame, drawable_width, drawable_height);
        } else if (presented_time <= 0.0 && callback_index == 1) {
            gfx_ios_diag_log("First SM64 drawable presentation callback has presentedTime=0; Apple reports the drawable as unpresented or dropped drawable=%p frame=%u", (void *)presented_drawable, submitted_frame);
        }
    }];
#endif
    [metal_command addScheduledHandler:^(id<MTLCommandBuffer> scheduled) {
        uint64_t scheduled_index;
        @synchronized ([NSObject class]) {
            scheduled_index = ++scheduled_command_count;
        }
        if (scheduled_index <= 3 || scheduled_index % 120 == 0) {
            gfx_ios_diag_log("Metal command buffer scheduled count=%llu frame=%u drawable=%p texture=%p status=%ld presentsWithTransaction=%d", (unsigned long long)scheduled_index, submitted_frame, (void *)submitted_drawable_identity, (void *)submitted_texture_identity, (long)scheduled.status, metal_view.presentsWithTransaction);
        }
    }];
    // Register presentation observation before scheduling the drawable. This
    // makes the callback ordering explicit and lets the log distinguish GPU
    // scheduling/completion from Core Animation actually displaying a frame.
    [metal_command presentDrawable:submitted_drawable];
    [metal_command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        if (completed.status == MTLCommandBufferStatusError) {
            gfx_ios_diag_log("Metal GPU command failed frame=%u status=%ld error=%s", submitted_frame, (long)completed.status, completed.error.localizedDescription.UTF8String ?: "unknown");
            NSLog(@"Metal GPU command failed frame=%u: %@", submitted_frame, completed.error.localizedDescription);
        } else {
            double gpu_start = completed.GPUStartTime;
            double gpu_end = completed.GPUEndTime;
            if (gpu_start > 0.0 && gpu_end >= gpu_start) {
                double gpu_ms = (gpu_end - gpu_start) * 1000.0;
                @synchronized ([NSObject class]) {
                    metal_performance_stats.gpu_frame_ms = gpu_ms;
                    metal_performance_stats.gpu_timing_available = true;
                }
                if (!metal_logged_gpu_timing) {
                    metal_logged_gpu_timing = true;
                    gfx_ios_diag_log("Metal GPU execution timing available firstFrame=%u elapsed=%.3fms; command-buffer GPU timestamps", submitted_frame, gpu_ms);
                }
            } else if (!metal_logged_gpu_timing_unavailable) {
                metal_logged_gpu_timing_unavailable = true;
                gfx_ios_diag_log("Metal GPU execution timing unavailable commandBuffer=%p GPUStartTime=%.6f GPUEndTime=%.6f", completed, gpu_start, gpu_end);
            }
            uint64_t completion_index;
            @synchronized ([NSObject class]) {
                completion_index = ++completed_drawable_count;
            }
            if (completion_index <= 3 || completion_index % 120 == 0) {
                gfx_ios_diag_log("Metal GPU command completion count=%llu frame=%u status=%ld drawable=%p size=%.0fx%.0f", (unsigned long long)completion_index, submitted_frame, (long)completed.status, (void *)submitted_drawable_identity, drawable_width, drawable_height);
            }
            if (!logged_first_completed_frame) {
                logged_first_completed_frame = true;
                gfx_ios_diag_log("Metal first game command buffer completed frame=%u drawable=%.0fx%.0f status=%ld", submitted_frame, drawable_width, drawable_height, (long)completed.status);
                gfx_ios_diag_mark_first_game_frame();
                NSLog(@"Metal first command buffer completed frame=%u drawable=%.0fx%.0f; presentation/runtime rendering still requires visual verification",
                      submitted_frame, drawable_width, drawable_height);
            }
        }
    }];
    [metal_command commit];
    if (submitted_frame <= 3 || submitted_frame % 120 == 0)
        gfx_ios_diag_log("Metal presentation heartbeat submittedFrame=%u drawable=%.0fx%.0f commandStatus=%ld sceneCanRender=%d", submitted_frame, drawable_width, drawable_height, (long)metal_command.status, gfx_metal_scene_can_render());
    metal_command = nil; metal_drawable = nil;
}
static void gfx_metal_finish_render(void) { [metal_command waitUntilCompleted]; }
static void gfx_metal_shutdown(void) {
    metal_encoder = nil; metal_drawable = nil; metal_command = nil;
    metal_scene_postprocessed_before_ui = false;
    gfx_metal_release_render_targets();
    [metal_ssao_pipeline release]; metal_ssao_pipeline = nil;
    [metal_ssao_denoise_pipeline release]; metal_ssao_denoise_pipeline = nil;
    [metal_scene_normals_pipeline release]; metal_scene_normals_pipeline = nil;
    [metal_bloom_extract_pipeline release]; metal_bloom_extract_pipeline = nil;
    [metal_bloom_blur_pipeline release]; metal_bloom_blur_pipeline = nil;
    [metal_shadow_pipeline release]; metal_shadow_pipeline = nil;
    [metal_god_ray_pipeline release]; metal_god_ray_pipeline = nil;
    [metal_shadow_depth_state release]; metal_shadow_depth_state = nil;
    [metal_shadow_vertex_buffer release]; metal_shadow_vertex_buffer = nil;
    free(metal_shadow_world_positions); metal_shadow_world_positions = NULL;
    [metal_present_pipeline release]; metal_present_pipeline = nil;
    [metal_present_sampler release]; metal_present_sampler = nil;
    struct MetalShader64Graph *old_shader_graph = metal_shader64_graph;
    metal_shader64_graph = NULL;
    gfx_metal_shader64_graph_release(old_shader_graph);
    [metal_shader64_pipeline release]; metal_shader64_pipeline = nil;
    [metal_shader64_library release]; metal_shader64_library = nil;
    metal_shader64_active_id[0] = '\0';
    for (size_t i = 0; i < program_count; ++i) {
        for (size_t j = 0; j < METAL_PIPELINE_STATE_CAP; ++j) [programs[i].pipelines[j] release];
        [programs[i].vertex release]; [programs[i].fragment release]; [programs[i].library release];
    }
    for (size_t i = 0; i < texture_count; ++i) [textures[i].texture release];
    [metal_samplers[0] release]; [metal_samplers[1] release];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) [depth_states[i][j] release];
    [metal_view removeFromSuperview]; [metal_view release]; [metal_queue release]; [metal_device release];
    metal_view = nil; metal_queue = nil; metal_device = nil;
    memset(programs, 0, sizeof(programs)); memset(textures, 0, sizeof(textures));
    program_count = 0; texture_count = 0; memset(depth_states, 0, sizeof(depth_states));
    frame_count = 0;
    shader64_start_time = 0.0;
    shader64_previous_time = 0.0;
    drawable_unavailable_count = 0;
    logged_pipeline_failures = 0;
    logged_pipeline_successes = 0;
    logged_draw_warnings = 0;
    logged_first_completed_frame = false;
    logged_first_presented_frame = false;
    logged_first_triangle_draw = false;
    metal_scene_active = false;
    inactive_scene_frame_count = 0;
    log_next_draw_rects = false;
    memset(bound_textures, 0, sizeof(bound_textures)); current_texture = NULL; active_program = NULL;
    metal_samplers[0] = nil; metal_samplers[1] = nil;
    sampler_linear[0] = sampler_linear[1] = false;
    sampler_cms[0] = sampler_cms[1] = 0;
    sampler_cmt[0] = sampler_cmt[1] = 0;
    sampler_state_valid[0] = sampler_state_valid[1] = false;
    sampler_restore_count = 0;
    sampler_anisotropy[0] = sampler_anisotropy[1] = 1;
    metal_active_sample_count = 1;
    metal_active_render_width = metal_active_render_height = 0;
    metal_frame_uses_offscreen = false;
    metal_offscreen_runtime_failed = false;
    metal_last_frame_start_time = 0.0;
    metal_logged_gpu_timing = metal_logged_gpu_timing_unavailable = false;
    metal_frame_draw_calls = metal_frame_triangles = 0;
    memset(&metal_performance_stats, 0, sizeof(metal_performance_stats));
}

struct GfxRenderingAPI gfx_metal_api = {
    gfx_metal_z_is_from_0_to_1, gfx_metal_unload_shader, gfx_metal_load_shader,
    gfx_metal_create_and_load_new_shader, gfx_metal_lookup_shader, gfx_metal_shader_get_info,
    gfx_metal_new_texture, gfx_metal_select_texture, gfx_metal_upload_texture,
    gfx_metal_set_sampler_parameters, gfx_metal_set_depth_test, gfx_metal_set_depth_mask,
    gfx_metal_set_zmode_decal, gfx_metal_set_viewport, gfx_metal_set_scissor,
    gfx_metal_set_use_alpha, gfx_metal_draw_triangles, gfx_metal_init, gfx_metal_on_resize,
    gfx_metal_start_frame, gfx_metal_end_frame, gfx_metal_finish_render, gfx_metal_shutdown, true,
    gfx_metal_begin_ui_overlay
};

#endif
