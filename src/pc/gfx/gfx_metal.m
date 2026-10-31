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

struct MetalProgram {
    uint32_t shader_id;
    id<MTLLibrary> library;
    id<MTLFunction> vertex;
    id<MTLFunction> fragment;
    struct CCFeatures features;
    id<MTLRenderPipelineState> pipelines[16];
};

struct MetalTexture {
    id<MTLTexture> texture;
    int width;
    int height;
};

struct MetalFragmentParams {
    uint32_t filter0;
    uint32_t filter1;
    float frame_count;
};

static id<MTLDevice> metal_device;
static id<MTLCommandQueue> metal_queue;
static MTKView *metal_view;
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
static bool log_next_draw_rects;
static uint32_t drawable_unavailable_count;
static uint32_t inactive_scene_frame_count;
static uint32_t logged_pipeline_failures;
static uint32_t logged_pipeline_successes;
static uint32_t logged_draw_warnings;
static struct MetalTexture *bound_textures[2];
static struct MetalTexture *current_texture;
static id<MTLSamplerState> metal_samplers[2];
static bool sampler_linear[2];
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
    gfx_ios_diag_log("Game Metal MTKView attached to SDL scene window frame=(%.0f,%.0f %.0fx%.0f) drawable=%.0fx%.0f scale=%.2f color=BGRA8Unorm depth=Depth32Float stencil=None parent=%@ window=%@ scene=%@ hidden=%d alpha=%.2f parentSubviewCount=%lu", metal_view.frame.origin.x, metal_view.frame.origin.y, metal_view.frame.size.width, metal_view.frame.size.height, drawable.width, drawable.height, metal_view.contentScaleFactor, metal_view.superview, metal_view.window, metal_view.window.windowScene.session.persistentIdentifier, metal_view.hidden, metal_view.alpha, (unsigned long)view_controller.view.subviews.count);
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
    if (texture_count <= 3 || texture_count % 128 == 0)
        gfx_ios_diag_log("Metal texture uploaded id=%lu size=%dx%d pixelFormat=RGBA8Unorm totalTextures=%lu", (unsigned long)(t - textures + 1), width, height, (unsigned long)texture_count);
}

static void gfx_metal_set_sampler_parameters(int sampler, bool linear, uint32_t cms, uint32_t cmt) {
    if (sampler < 0 || sampler > 1 || !metal_device) return;
    MTLSamplerDescriptor *desc = [[[MTLSamplerDescriptor alloc] init] autorelease];
    [metal_samplers[sampler] release];
    sampler_linear[sampler] = linear;
    desc.minFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    desc.magFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    enum GfxSamplerAddressMode s_mode = gfx_metal_sampler_address_mode(cms);
    enum GfxSamplerAddressMode t_mode = gfx_metal_sampler_address_mode(cmt);
    desc.sAddressMode = s_mode == GFX_SAMPLER_CLAMP ? MTLSamplerAddressModeClampToEdge :
                        (s_mode == GFX_SAMPLER_MIRROR ? MTLSamplerAddressModeMirrorRepeat : MTLSamplerAddressModeRepeat);
    desc.tAddressMode = t_mode == GFX_SAMPLER_CLAMP ? MTLSamplerAddressModeClampToEdge :
                        (t_mode == GFX_SAMPLER_MIRROR ? MTLSamplerAddressModeMirrorRepeat : MTLSamplerAddressModeRepeat);
    metal_samplers[sampler] = [metal_device newSamplerStateWithDescriptor:desc];
    if (!metal_samplers[sampler])
        NSLog(@"Metal sampler creation failed for tile=%d linear=%d address=(%u,%u)", sampler, linear, cms, cmt);
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
    NSUInteger key = gfx_metal_pipeline_state_key(state_depth_test, state_depth_write,
                                                   state_blend, state_decal);
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
            gfx_ios_diag_log("Metal render pipeline creation failed shader=0x%08x state=0x%lx colorFormat=%lu depthFormat=%lu error=%s", p->shader_id, (unsigned long)key, (unsigned long)metal_view.colorPixelFormat, (unsigned long)metal_view.depthStencilPixelFormat, error.localizedDescription.UTF8String ?: "unknown");
        NSLog(@"Metal pipeline creation failed shader=0x%08x state=0x%lx: %@",
              p->shader_id, (unsigned long)key, error.localizedDescription);
    } else if (logged_pipeline_successes++ < 16) {
        gfx_ios_diag_log("Metal pipeline created shader=0x%08x state=0x%lx", p->shader_id, (unsigned long)key);
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
    if (active_program->features.used_textures[0] || active_program->features.used_textures[1] ||
        active_program->features.opt_noise) {
        struct MetalFragmentParams fragment_params = {
            configFiltering == 2 && sampler_linear[0] ? 1u : 0u,
            configFiltering == 2 && sampler_linear[1] ? 1u : 0u,
            (float)frame_count
        };
        id<MTLBuffer> params = [metal_device newBufferWithBytes:&fragment_params length:sizeof(fragment_params)
                                                        options:MTLResourceStorageModeShared];
        if (!params) {
            if (logged_draw_warnings++ < 12) gfx_ios_diag_log("Metal fragment parameter buffer allocation failed shader=0x%08x", active_program->shader_id);
            [buffer release]; return;
        }
        [metal_encoder setFragmentBuffer:params offset:0 atIndex:1];
        [params release];
    }
    NSUInteger width = (NSUInteger)MAX(0, metal_view.drawableSize.width);
    NSUInteger height = (NSUInteger)MAX(0, metal_view.drawableSize.height);
    double scale = metal_view.contentScaleFactor;
    MTLViewport vp = {0, 0, (double)width, (double)height, 0.0, 1.0};
    if (has_viewport) {
        double x = viewport[0] * scale, y = viewport[1] * scale;
        double w = viewport[2] * scale, h = viewport[3] * scale;
        vp = (MTLViewport){x, height - y - h, w, h, 0.0, 1.0};
    }
    [metal_encoder setViewport:vp];
    NSInteger scissor_x = 0, scissor_y = 0;
    NSInteger scissor_width = (NSInteger)width, scissor_height = (NSInteger)height;
    if (has_scissor) {
        scissor_x = (NSInteger)(scissor[0] * scale);
        scissor_y = (NSInteger)(height - (scissor[1] + scissor[3]) * scale);
        scissor_width = (NSInteger)(scissor[2] * scale);
        scissor_height = (NSInteger)(scissor[3] * scale);
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
        gfx_ios_diag_log("Metal post-resume draw rectangles shader=0x%08x hasViewport=%d logicalViewport=(%d,%d %dx%d) effectiveViewport=(%.1f,%.1f %.1fx%.1f) hasScissor=%d logicalScissor=(%d,%d %dx%d) effectiveScissor=(%ld,%ld %ldx%ld) drawable=%lux%lu scale=%.2f", active_program->shader_id, has_viewport, viewport[0], viewport[1], viewport[2], viewport[3], vp.originX, vp.originY, vp.width, vp.height, has_scissor, scissor[0], scissor[1], scissor[2], scissor[3], (long)scissor_x, (long)scissor_y, (long)scissor_width, (long)scissor_height, (unsigned long)width, (unsigned long)height, scale);
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
    if (!logged_first_triangle_draw) {
        logged_first_triangle_draw = true;
        gfx_ios_diag_log("Metal first game triangle draw encoded shader=0x%08x triangles=%lu vertices=%lu drawable=%.0fx%.0f", active_program->shader_id, (unsigned long)triangle_count, (unsigned long)(triangle_count * 3), metal_view.drawableSize.width, metal_view.drawableSize.height);
    }
    [buffer release];
}

static void gfx_metal_init(void) {
    gfx_ios_diag_log("Metal graphics API init entered backend=RAPI_METAL device=%@ view=%@ drawable=%.0fx%.0f", metal_device.name, metal_view, metal_view.drawableSize.width, metal_view.drawableSize.height);
    if (!metal_device) metal_device = MTLCreateSystemDefaultDevice();
    if (!metal_device) { gfx_ios_diag_log("Metal graphics API init failed: no Metal device available"); NSLog(@"Metal initialization failed: no Metal device available"); return; }
    if (!metal_queue) metal_queue = [metal_device newCommandQueue];
    if (!metal_queue) { gfx_ios_diag_log("Metal graphics API init failed: command queue unavailable"); NSLog(@"Metal initialization failed: could not create command queue"); return; }
    if (!metal_view) { gfx_ios_diag_log("Metal graphics API init warning: no MTKView was attached before renderer initialization"); NSLog(@"Metal view was not attached before renderer initialization"); }
    texture_count = 0;
    for (int i = 0; i < 2; ++i) {
        MTLSamplerDescriptor *desc = [[[MTLSamplerDescriptor alloc] init] autorelease];
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.magFilter = MTLSamplerMinMagFilterNearest;
        desc.sAddressMode = MTLSamplerAddressModeRepeat;
        desc.tAddressMode = MTLSamplerAddressModeRepeat;
        metal_samplers[i] = [metal_device newSamplerStateWithDescriptor:desc];
        if (!metal_samplers[i]) gfx_ios_diag_log("Metal default sampler creation failed tile=%d", i);
    }
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
    MTLRenderPassDescriptor *pass = metal_view.currentRenderPassDescriptor;
    metal_drawable = [metal_view currentDrawable];
    if (!metal_drawable || !pass) {
        uint32_t unavailable = ++drawable_unavailable_count;
        if (unavailable <= 5 || unavailable % 60 == 0)
            gfx_ios_diag_log("Metal frame drawable unavailable count=%u pass=%s drawable=%s viewFrame=(%.0f,%.0f %.0fx%.0f) drawableSize=%.0fx%.0f attachedWindow=%@ scene=%@ windowHidden=%d viewHidden=%d appState=%ld", unavailable, pass ? "yes" : "nil", metal_drawable ? "yes" : "nil", metal_view.frame.origin.x, metal_view.frame.origin.y, metal_view.frame.size.width, metal_view.frame.size.height, metal_view.drawableSize.width, metal_view.drawableSize.height, metal_view.window, metal_view.window.windowScene.session.persistentIdentifier, metal_view.window.hidden, metal_view.hidden, (long)UIApplication.sharedApplication.applicationState);
        metal_drawable = nil;
        return;
    }
    metal_command = [metal_queue commandBuffer];
    if (!metal_command) {
        gfx_ios_diag_log("Metal frame %u could not create command buffer queue=%@ drawable=%@", frame_count + 1, metal_queue, metal_drawable);
        NSLog(@"Metal frame %u could not create a command buffer", frame_count + 1);
        metal_drawable = nil;
        return;
    }
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
    metal_encoder = [metal_command renderCommandEncoderWithDescriptor:pass];
    if (!metal_encoder) {
        gfx_ios_diag_log("Metal frame %u could not create render encoder pass=%@ drawable=%@", frame_count + 1, pass, metal_drawable);
        NSLog(@"Metal frame %u could not create a render encoder", frame_count + 1);
        metal_command = nil;
        metal_drawable = nil;
        return;
    }
    ++frame_count;
    if (frame_count == 1 || frame_count == 2 || frame_count == 30)
        gfx_ios_diag_log("Metal game frame encoding started frame=%u drawable=%.0fx%.0f viewVisible=%d alpha=%.2f scene=%@", frame_count, metal_view.drawableSize.width, metal_view.drawableSize.height, !metal_view.hidden && metal_view.window && !metal_view.window.hidden, metal_view.alpha, metal_view.window.windowScene.session.persistentIdentifier);
    has_viewport = has_scissor = false;
}
static void gfx_metal_end_frame(void) {
    if (!metal_encoder) return;
    [metal_encoder endEncoding]; metal_encoder = nil;
    id<MTLDrawable> submitted_drawable = (id<MTLDrawable>)metal_drawable;
    double drawable_width = metal_view.drawableSize.width;
    double drawable_height = metal_view.drawableSize.height;
    [metal_command presentDrawable:submitted_drawable];
#if !TARGET_OS_SIMULATOR
    [submitted_drawable addPresentedHandler:^(id<MTLDrawable> presented_drawable) {
        if (!logged_first_presented_frame) {
            logged_first_presented_frame = true;
            gfx_ios_diag_log("First SM64 game drawable presented by CoreAnimation presentedTime=%.6f size=%.0fx%.0f", presented_drawable.presentedTime, drawable_width, drawable_height);
        }
    }];
#endif
    uint32_t submitted_frame = frame_count;
    [metal_command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        if (completed.status == MTLCommandBufferStatusError) {
            gfx_ios_diag_log("Metal GPU command failed frame=%u status=%ld error=%s", submitted_frame, (long)completed.status, completed.error.localizedDescription.UTF8String ?: "unknown");
            NSLog(@"Metal GPU command failed frame=%u: %@", submitted_frame, completed.error.localizedDescription);
        } else if (!logged_first_completed_frame) {
            logged_first_completed_frame = true;
            gfx_ios_diag_log("Metal first game command buffer completed frame=%u drawable=%.0fx%.0f status=%ld", submitted_frame, drawable_width, drawable_height, (long)completed.status);
            gfx_ios_diag_mark_first_game_frame();
            NSLog(@"Metal first command buffer completed frame=%u drawable=%.0fx%.0f; presentation/runtime rendering still requires visual verification",
                  submitted_frame, drawable_width, drawable_height);
        }
    }];
    [metal_command commit];
    if (submitted_frame <= 3 || submitted_frame % 60 == 0)
        gfx_ios_diag_log("Metal presentDrawable submitted frame=%u drawable=%.0fx%.0f commandStatus=%ld", submitted_frame, drawable_width, drawable_height, (long)metal_command.status);
    metal_command = nil; metal_drawable = nil;
}
static void gfx_metal_finish_render(void) { [metal_command waitUntilCompleted]; }
static void gfx_metal_shutdown(void) {
    metal_encoder = nil; metal_drawable = nil; metal_command = nil;
    for (size_t i = 0; i < program_count; ++i) {
        for (size_t j = 0; j < 16; ++j) [programs[i].pipelines[j] release];
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
