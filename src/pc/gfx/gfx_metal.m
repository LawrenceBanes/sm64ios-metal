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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "gfx_metal.h"
#include "gfx_metal_state.h"
#include "gfx_metal_shader.h"
#include "gfx_metal_vertex_layout.h"
#include "gfx_cc.h"
#include "gfx_uikit.h"
#include "../configfile.h"

#define METAL_SHADER_CAP 128
#define METAL_TEXTURE_CAP 8192
#define METAL_PIPELINE_STATE_CAP 48

struct MetalProgram {
    uint32_t shader_id;
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
    float frame_count;
};

static id<MTLDevice> metal_device;
static id<MTLCommandQueue> metal_queue;
static MTKView *metal_view;
static id<MTLTexture> metal_depth_texture;
static NSUInteger metal_depth_width;
static NSUInteger metal_depth_height;
static NSUInteger metal_depth_samples = 1;
static id<MTLTexture> metal_color_target;
static id<MTLTexture> metal_multisample_color;
static id<MTLTexture> metal_resolve_texture;
static NSUInteger metal_target_width;
static NSUInteger metal_target_height;
static NSUInteger metal_target_samples;
static id<MTLRenderPipelineState> metal_present_pipeline;
static id<MTLSamplerState> metal_present_sampler;
static struct GfxMetalSettings metal_settings = { GFX_METAL_PRESET_CLASSIC, 100u, 1u, 1u };
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
static bool metal_offscreen_runtime_failed;
static id<MTLCommandBuffer> metal_command;
static id<MTLRenderCommandEncoder> metal_encoder;
static id<CAMetalDrawable> metal_drawable;
static struct MetalProgram programs[METAL_SHADER_CAP];
static size_t program_count;
static struct MetalTexture textures[METAL_TEXTURE_CAP];
static size_t texture_count;
static uint32_t frame_count;
static bool metal_scene_active;
static bool logged_first_completed_frame;
static bool logged_first_presented_frame;
static bool logged_first_triangle_draw;
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
    descriptor.usage = MTLTextureUsageRenderTarget;
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
    metal_depth_width = metal_depth_height = 0;
    metal_depth_samples = 1;
    metal_target_width = metal_target_height = metal_target_samples = 0;
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
    struct GfxMetalSettings requested = gfx_metal_classic_settings();
    if ([defaults objectForKey:@"SM64MetalPreset"]) {
        requested.preset = (uint32_t)[defaults integerForKey:@"SM64MetalPreset"];
        requested = requested.preset == GFX_METAL_PRESET_ENHANCED ?
                    gfx_metal_enhanced_settings() : gfx_metal_classic_settings();
    }
    if ([defaults objectForKey:@"SM64MetalRenderScale"])
        requested.render_scale_percent = (uint32_t)[defaults integerForKey:@"SM64MetalRenderScale"];
    if ([defaults objectForKey:@"SM64MetalSampleCount"])
        requested.sample_count = (uint32_t)[defaults integerForKey:@"SM64MetalSampleCount"];
    if ([defaults objectForKey:@"SM64MetalAnisotropy"])
        requested.anisotropy = (uint32_t)[defaults integerForKey:@"SM64MetalAnisotropy"];
    return gfx_metal_settings_sanitize(requested, metal_capabilities);
}

static void gfx_metal_persist_settings(struct GfxMetalSettings settings) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:(NSInteger)settings.preset forKey:@"SM64MetalPreset"];
    [defaults setInteger:(NSInteger)settings.render_scale_percent forKey:@"SM64MetalRenderScale"];
    [defaults setInteger:(NSInteger)settings.sample_count forKey:@"SM64MetalSampleCount"];
    [defaults setInteger:(NSInteger)settings.anisotropy forKey:@"SM64MetalAnisotropy"];
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
                       metal_settings.anisotropy != sanitized.anisotropy;
        metal_settings = sanitized;
        if (targets_changed || !gfx_metal_settings_use_offscreen(sanitized))
            metal_offscreen_runtime_failed = false;
        metal_performance_stats.preset = sanitized.preset;
        metal_performance_stats.render_scale_percent = sanitized.render_scale_percent;
        metal_performance_stats.sample_count = sanitized.sample_count;
        metal_performance_stats.anisotropy = sanitized.anisotropy;
        if (targets_changed) gfx_metal_release_render_targets();
        if (changed) {
            gfx_ios_diag_log("Metal graphics settings applied preset=%s scale=%u%% samples=%ux anisotropy=%u", sanitized.preset == GFX_METAL_PRESET_CLASSIC ? "Classic" : "Enhanced", sanitized.render_scale_percent, sanitized.sample_count, sanitized.anisotropy);
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
    bool ready = gfx_metal_surface_is_ready();
    gfx_ios_diag_log("Game Metal surface readiness=%s drawable=%.0fx%.0f attachedToScene=%d", ready ? "ready" : "not-ready", drawable.width, drawable.height, metal_view.window.windowScene != nil);
    return ready;
}

static struct MetalProgram *find_program(uint32_t shader_id) {
    for (size_t i = 0; i < program_count; ++i)
        if (programs[i].shader_id == shader_id) return &programs[i];
    return NULL;
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
    struct MetalProgram *p = find_program(shader_id);
    if (p) return p;
    if (program_count >= METAL_SHADER_CAP) return NULL;
    char source[8192];
    struct CCFeatures features;
    NSError *error = nil;
    if (!gfx_metal_build_shader(shader_id, source, sizeof(source), &features)) {
        gfx_ios_diag_log("Metal shader generation failed shader=0x%08x", shader_id);
        NSLog(@"Metal shader generation failed for shader=0x%08x", shader_id);
        return NULL;
    }
    NSString *msl = [NSString stringWithUTF8String:source];
    id<MTLLibrary> library = [metal_device newLibraryWithSource:msl options:nil error:&error];
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
    p = &programs[program_count++];
    memset(p, 0, sizeof(*p));
    p->shader_id = shader_id;
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
    desc.colorAttachments[0].pixelFormat = metal_view.colorPixelFormat;
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
        (float)frame_count
    };
    [metal_encoder setFragmentBytes:&fragment_params length:sizeof(fragment_params) atIndex:1];

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

static void gfx_metal_fallback_to_direct(const char *reason) {
    struct GfxMetalSettings fallback = metal_settings;
    fallback.render_scale_percent = 100u;
    fallback.sample_count = 1u;
    gfx_ios_diag_log("Metal offscreen rendering fallback reason=%s; active render target is drawable at 100%%/1x", reason ?: "unknown");
    gfx_metal_apply_settings(fallback);
}

static bool gfx_metal_ensure_offscreen_targets(id<CAMetalDrawable> drawable) {
    if (!gfx_metal_settings_use_offscreen(metal_settings)) return false;
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
    NSUInteger samples = metal_settings.sample_count;
    BOOL target_ready = metal_target_width == size.width && metal_target_height == size.height &&
        metal_target_samples == samples && metal_depth_texture != nil &&
        (samples > 1 ? (metal_multisample_color && metal_resolve_texture) : metal_color_target != nil);
    if (target_ready) return true;

    gfx_metal_release_render_targets();
    if (samples > 1) {
        metal_multisample_color = gfx_metal_new_target_texture(size.width, size.height, samples,
            metal_view.colorPixelFormat, MTLTextureUsageRenderTarget);
        metal_resolve_texture = gfx_metal_new_target_texture(size.width, size.height, 1,
            metal_view.colorPixelFormat, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        if (metal_multisample_color) metal_multisample_color.label = @"SM64 MSAA color";
        if (metal_resolve_texture) metal_resolve_texture.label = @"SM64 resolved color";
    } else {
        metal_color_target = gfx_metal_new_target_texture(size.width, size.height, 1,
            metal_view.colorPixelFormat, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        if (metal_color_target) metal_color_target.label = @"SM64 scaled color";
    }
    id<MTLTexture> depth = gfx_metal_depth_for_size(size.width, size.height, samples);
    if ((samples > 1 && (!metal_multisample_color || !metal_resolve_texture)) ||
        (samples == 1 && !metal_color_target) || !depth) {
        gfx_metal_release_render_targets();
        gfx_metal_fallback_to_direct("offscreen color/depth allocation failed");
        return false;
    }
    metal_target_width = size.width;
    metal_target_height = size.height;
    metal_target_samples = samples;
    gfx_ios_diag_log("Metal offscreen targets ready size=%lux%lu scale=%u%% samples=%lu color=%@ resolve=%@ depth=%@", (unsigned long)size.width, (unsigned long)size.height, metal_settings.render_scale_percent, (unsigned long)samples, samples > 1 ? metal_multisample_color : metal_color_target, samples > 1 ? metal_resolve_texture : nil, depth);
    return true;
}

static bool gfx_metal_encode_final_pass(id<CAMetalDrawable> drawable,
                                        id<MTLCommandBuffer> command) {
    id<MTLTexture> source = metal_active_sample_count > 1 ? metal_resolve_texture : metal_color_target;
    if (!source || !metal_present_pipeline || !metal_present_sampler) {
        gfx_ios_diag_log("Metal final pass skipped source=%@ pipeline=%@ sampler=%@", source, metal_present_pipeline, metal_present_sampler);
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
        return false;
    }
    [encoder setRenderPipelineState:metal_present_pipeline];
    [encoder setFragmentTexture:source atIndex:0];
    [encoder setFragmentSamplerState:metal_present_sampler atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
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
    gfx_ios_diag_log("Metal startup graphics settings preset=%s scale=%u%% samples=%ux anisotropy=%u", metal_settings.preset == GFX_METAL_PRESET_CLASSIC ? "Classic" : "Enhanced", metal_settings.render_scale_percent, metal_settings.sample_count, metal_settings.anisotropy);
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
    if (metal_offscreen_runtime_failed && gfx_metal_settings_use_offscreen(metal_settings)) {
        metal_offscreen_runtime_failed = false;
        gfx_metal_fallback_to_direct("enhanced pipeline or final-pass encoder failed");
    }
    metal_frame_uses_offscreen = metal_drawable && gfx_metal_ensure_offscreen_targets(metal_drawable);
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
            pass.depthAttachment.storeAction = MTLStoreActionDontCare;
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
    if (!metal_encoder) return;
    [metal_encoder endEncoding]; metal_encoder = nil;
    if (metal_frame_uses_offscreen && !gfx_metal_encode_final_pass(metal_drawable, metal_command))
        metal_offscreen_runtime_failed = true;
    @synchronized ([NSObject class]) {
        metal_performance_stats.draw_calls = metal_frame_draw_calls;
        metal_performance_stats.triangles = metal_frame_triangles;
    }
    id<CAMetalDrawable> submitted_drawable = metal_drawable;
    double drawable_width = metal_view.drawableSize.width;
    double drawable_height = metal_view.drawableSize.height;
    uint32_t submitted_frame = frame_count;
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
    gfx_metal_release_render_targets();
    [metal_present_pipeline release]; metal_present_pipeline = nil;
    [metal_present_sampler release]; metal_present_sampler = nil;
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
    gfx_metal_start_frame, gfx_metal_end_frame, gfx_metal_finish_render, gfx_metal_shutdown
};

#endif
