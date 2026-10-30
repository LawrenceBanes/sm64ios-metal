#if defined(__APPLE__)
#import <TargetConditionals.h>
#elif !defined(TARGET_OS_IOS)
#define TARGET_OS_IOS 0
#endif

#if defined(RAPI_METAL) && TARGET_OS_IOS

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdlib.h>
#include <string.h>
#include "gfx_metal.h"
#include "gfx_metal_state.h"
#include "gfx_metal_shader.h"
#include "gfx_cc.h"
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

static bool gfx_metal_z_is_from_0_to_1(void) { return true; }

void gfx_metal_attach_to_view_controller(UIViewController *view_controller) {
    if (!view_controller || metal_view) return;
    metal_device = MTLCreateSystemDefaultDevice();
    if (!metal_device) return;
    metal_queue = [metal_device newCommandQueue];
    metal_view = [[MTKView alloc] initWithFrame:view_controller.view.bounds device:metal_device];
    metal_view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    metal_view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    metal_view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    metal_view.framebufferOnly = YES;
    metal_view.paused = YES;
    metal_view.enableSetNeedsDisplay = NO;
    metal_view.opaque = YES;
    metal_view.contentScaleFactor = UIScreen.mainScreen.scale;
    metal_view.backgroundColor = UIColor.blackColor;
    [view_controller.view insertSubview:metal_view atIndex:0];
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
    if (texture_count >= METAL_TEXTURE_CAP) return 0;
    return (uint32_t)++texture_count;
}

static void gfx_metal_select_texture(int tile, uint32_t texture_id) {
    if (tile < 0 || tile > 1 || texture_id == 0 || texture_id > texture_count) return;
    current_texture = &textures[texture_id - 1];
    bound_textures[tile] = current_texture;
}

static void gfx_metal_upload_texture(const uint8_t *rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return;
    struct MetalTexture *t = current_texture;
    if (!t) return;
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                   width:(NSUInteger)width
                                                                                  height:(NSUInteger)height
                                                                               mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    [t->texture release];
    t->texture = [metal_device newTextureWithDescriptor:desc];
    if (!t->texture) return;
    [t->texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
                    withBytes:rgba bytesPerRow:(NSUInteger)width * 4];
    t->width = width;
    t->height = height;
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
    if (!gfx_metal_build_shader(shader_id, source, sizeof(source), &features)) return NULL;
    NSString *msl = [NSString stringWithUTF8String:source];
    id<MTLLibrary> library = [metal_device newLibraryWithSource:msl options:nil error:&error];
    if (!library) { NSLog(@"Metal shader build failed: %@", error); return NULL; }
    id<MTLFunction> vertex = [library newFunctionWithName:@"sm64_vertex"];
    id<MTLFunction> fragment = [library newFunctionWithName:@"sm64_fragment"];
    if (!vertex || !fragment) {
        NSLog(@"Generated Metal shader is missing a stage function");
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
    return p;
}

static id<MTLRenderPipelineState> pipeline_for(struct MetalProgram *p) {
    NSUInteger key = gfx_metal_pipeline_state_key(state_depth_test, state_depth_write,
                                                   state_blend, state_decal);
    if (p->pipelines[key]) return p->pipelines[key];
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    NSUInteger offset = 0, attr = 0;
    vd.attributes[attr].format = MTLVertexFormatFloat4;
    vd.attributes[attr].offset = offset; vd.attributes[attr++].bufferIndex = 0; offset += 16;
    if (p->features.used_textures[0] || p->features.used_textures[1]) {
        vd.attributes[attr].format = MTLVertexFormatFloat2;
        vd.attributes[attr].offset = offset; vd.attributes[attr++].bufferIndex = 0; offset += 8;
    }
    if (p->features.opt_fog) {
        vd.attributes[attr].format = MTLVertexFormatFloat4;
        vd.attributes[attr].offset = offset; vd.attributes[attr++].bufferIndex = 0; offset += 16;
    }
    for (int i = 0; i < p->features.num_inputs; ++i) {
        vd.attributes[attr].format = p->features.opt_alpha ? MTLVertexFormatFloat4 : MTLVertexFormatFloat3;
        vd.attributes[attr].offset = offset; vd.attributes[attr++].bufferIndex = 0;
        offset += p->features.opt_alpha ? 16 : 12;
    }
    vd.layouts[0].stride = offset;
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
    if (!p->pipelines[key]) NSLog(@"Metal pipeline creation failed: %@", error);
    return p->pipelines[key];
}

static void gfx_metal_draw_triangles(float data[], size_t float_count, size_t triangle_count) {
    if (!metal_encoder || !active_program || !data || !triangle_count) return;
    id<MTLRenderPipelineState> pipeline = pipeline_for(active_program);
    if (!pipeline) return;
    id<MTLBuffer> buffer = [metal_device newBufferWithBytes:data length:float_count * sizeof(float) options:MTLResourceStorageModeShared];
    if (!buffer) return;
    [metal_encoder setRenderPipelineState:pipeline];
    [metal_encoder setVertexBuffer:buffer offset:0 atIndex:0];
    if (active_program->features.used_textures[0]) {
        if (!bound_textures[0] || !bound_textures[0]->texture || !metal_samplers[0]) { [buffer release]; return; }
        [metal_encoder setFragmentTexture:bound_textures[0]->texture atIndex:0];
        [metal_encoder setFragmentSamplerState:metal_samplers[0] atIndex:0];
    }
    if (active_program->features.used_textures[1]) {
        if (!bound_textures[1] || !bound_textures[1]->texture || !metal_samplers[1]) { [buffer release]; return; }
        [metal_encoder setFragmentTexture:bound_textures[1]->texture atIndex:1];
        [metal_encoder setFragmentSamplerState:metal_samplers[1] atIndex:1];
    }
    if (active_program->features.used_textures[0] || active_program->features.used_textures[1]) {
        uint32_t filter_params[2] = {
            configFiltering == 2 && sampler_linear[0] ? 1u : 0u,
            configFiltering == 2 && sampler_linear[1] ? 1u : 0u
        };
        id<MTLBuffer> params = [metal_device newBufferWithBytes:filter_params length:sizeof(filter_params)
                                                        options:MTLResourceStorageModeShared];
        if (!params) { [buffer release]; return; }
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
    if (has_scissor) {
        NSInteger sx = (NSInteger)(scissor[0] * scale);
        NSInteger sy = (NSInteger)(height - (scissor[1] + scissor[3]) * scale);
        NSInteger sw = (NSInteger)(scissor[2] * scale), sh = (NSInteger)(scissor[3] * scale);
        sx = MIN(MAX(sx, 0), (NSInteger)width);
        sy = MIN(MAX(sy, 0), (NSInteger)height);
        sw = MIN(sw, (NSInteger)width - sx); sh = MIN(sh, (NSInteger)height - sy);
        if (sw < 0) sw = 0;
        if (sh < 0) sh = 0;
        MTLScissorRect sr = {(NSUInteger)sx, (NSUInteger)sy, (NSUInteger)sw, (NSUInteger)sh};
        [metal_encoder setScissorRect:sr];
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
    [buffer release];
}

static void gfx_metal_init(void) {
    if (!metal_device) metal_device = MTLCreateSystemDefaultDevice();
    if (!metal_device) { NSLog(@"No Metal device available"); return; }
    if (!metal_queue) metal_queue = [metal_device newCommandQueue];
    if (!metal_view) NSLog(@"Metal view was not attached before renderer initialization");
    texture_count = 0;
    for (int i = 0; i < 2; ++i) {
        MTLSamplerDescriptor *desc = [[[MTLSamplerDescriptor alloc] init] autorelease];
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.magFilter = MTLSamplerMinMagFilterNearest;
        desc.sAddressMode = MTLSamplerAddressModeRepeat;
        desc.tAddressMode = MTLSamplerAddressModeRepeat;
        metal_samplers[i] = [metal_device newSamplerStateWithDescriptor:desc];
    }
}
static void gfx_metal_on_resize(void) { if (metal_view) [metal_view setNeedsLayout]; }
static void gfx_metal_start_frame(void) {
    if (!metal_view || !metal_queue) return;
    MTLRenderPassDescriptor *pass = metal_view.currentRenderPassDescriptor;
    metal_drawable = [metal_view currentDrawable];
    if (!metal_drawable || !pass) return;
    metal_command = [metal_queue commandBuffer];
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
    metal_encoder = [metal_command renderCommandEncoderWithDescriptor:pass];
    has_viewport = has_scissor = false;
}
static void gfx_metal_end_frame(void) {
    if (!metal_encoder) return;
    [metal_encoder endEncoding]; metal_encoder = nil;
    [metal_command presentDrawable:metal_drawable];
    [metal_command commit];
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
