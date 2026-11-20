/* Host replay through the production decoder, Gfx interpreter, and partition. */
#include <assert.h>
#include <stdlib.h>
#include "src/pc/gfx/gfx_pc.c"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "game/area.h"
#include "game/object_list_processor.h"
#include "pc/gfx/gfx_metal_shader.h"
#include "level_table.h"

SpatialPartitionCell gStaticSurfacePartition[SURFACE_PARTITION_DIM][SURFACE_PARTITION_DIM];
SpatialPartitionCell gDynamicSurfacePartition[SURFACE_PARTITION_DIM][SURFACE_PARTITION_DIM];
struct NumTimesCalled gNumCalls;
struct Object *gCurrentObject;
struct Object *gMarioObject;
static struct MarioState test_mario;
struct MarioState *gMarioState = &test_mario;
s16 gCheckingSurfaceCollisionsForCamera;
s16 gFindFloorIncludeSurfaceIntangible;
s32 gNumFindFloorMisses;
unsigned int configFiltering = 1;

/* The package is real. The ordinary area services are stubbed so the test can
 * call the shipping area hooks without game assets or an Objective-C host. */
struct GraphNode gObjParentGraphNode;
static struct Object inactive_object;
static bool base_terrain_loaded;

void load_area_terrain(s16 index, s16 *data, s8 *rooms, s16 *macroObjects) {
    assert(index > 0 && index <= SCENE_EXTERNAL_AREA && data && !rooms && !macroObjects);
    assert(world_region_count() == 0);
    base_terrain_loaded = true;
}

void spawn_objects_from_info(s32 index, struct SpawnInfo *info) {
    assert(index == 0 && info && base_terrain_loaded);
    assert(area_scene_package_id() == 513 && world_region_count() == 1);
}

void unload_objects_from_area(s32 index, s32 area) {
    assert(index == 0 && area > 0 && area <= SCENE_EXTERNAL_AREA);
    if (area != SCENE_EXTERNAL_AREA) assert(world_region_count() == 1);
}

void geo_call_global_function_nodes(struct GraphNode *node, s32 context) {
    (void)node;
    assert(context == GEO_CONTEXT_AREA_LOAD || context == GEO_CONTEXT_AREA_UNLOAD);
}

void *virtual_to_segmented(u32 segment, const void *address) {
    (void)segment;
    return (void *)address;
}

#define WARP_STUB(name) const BehaviorScript name[] = {0};
WARP_STUB(bhvDoorWarp)
WARP_STUB(bhvStar)
WARP_STUB(bhvExitPodiumWarp)
WARP_STUB(bhvWarp)
WARP_STUB(bhvWarpPipe)
WARP_STUB(bhvFadingWarp)
WARP_STUB(bhvInstantActiveWarp)
WARP_STUB(bhvAirborneWarp)
WARP_STUB(bhvHardAirKnockBackWarp)
WARP_STUB(bhvSpinAirborneCircleWarp)
WARP_STUB(bhvDeathWarp)
WARP_STUB(bhvSpinAirborneWarp)
WARP_STUB(bhvFlyingWarp)
WARP_STUB(bhvSwimmingWarp)
WARP_STUB(bhvPaintingStarCollectWarp)
WARP_STUB(bhvPaintingDeathWarp)
WARP_STUB(bhvAirborneStarCollectWarp)
WARP_STUB(bhvAirborneDeathWarp)
WARP_STUB(bhvLaunchStarCollectWarp)
WARP_STUB(bhvLaunchDeathWarp)
#undef WARP_STUB

s32 mario_ios_portal_surface_passable(const struct Surface *surface, f32 x, f32 y, f32 z,
                                      f32 radius) {
    (void)surface; (void)x; (void)y; (void)z; (void)radius;
    return 0;
}

struct ShaderProgram { u32 id; };
static struct ShaderProgram shaders[32];
static unsigned shaderCount;
static unsigned triangles, textures;
static bool check_materials;
static bool check_diagnostic;
static bool check_wsc3;
static bool check_wsc4;
static bool check_wsc5;
static bool check_scene_probe;
static bool check_import;
static unsigned wsc3_draws, bound_ids[2], bound_s[2], bound_t[2];
static unsigned wsc4_draws;
static unsigned wsc5_draws;
static unsigned scene_probe_draws, scene_probe_uploads;
static float wsc3_first_uv[4], wsc3_first_alpha;
static unsigned translucent_triangles;

static struct ShaderProgram *lookup(u32 id) {
    for (unsigned i = 0; i < shaderCount; ++i) {
        if (shaders[i].id == id) return &shaders[i];
    }
    return NULL;
}
static struct ShaderProgram *create(u32 id) {
    assert(shaderCount < sizeof(shaders) / sizeof(shaders[0]));
    shaders[shaderCount].id = id;
    return &shaders[shaderCount++];
}
static void bind_shader(struct ShaderProgram *program) { (void)program; }
static void shader_info(struct ShaderProgram *program, u8 *inputs, bool used[2]) {
    struct CCFeatures f;
    gfx_cc_get_features(program->id, &f);
    *inputs = f.num_inputs;
    memcpy(used, f.used_textures, sizeof(f.used_textures));
}
static u32 new_texture(void) { return 1 + textures; }
static bool depth_range(void) { return true; }
static void toggle(bool enabled) { (void)enabled; }
static void rectangle(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void select_texture(int tile, u32 id) {
    assert(tile >= 0 && tile <= ((check_wsc3 || check_import || check_scene_probe) ? 1 : 0) && id > 0);
    bound_ids[tile] = id;
}
static void sampler(int tile, bool linear, u32 s, u32 t) {
    assert(tile >= 0 && tile <= ((check_wsc3 || check_import || check_scene_probe) ? 1 : 0));
    (void)linear;
    bound_s[tile] = s;
    bound_t[tile] = t;
}
static void upload(const u8 *rgba, int width, int height) {
    if (check_scene_probe) {
        assert(width > 0 && height > 0 && rgba);
        ++scene_probe_uploads;
    } else if (check_import) {
        assert(width == 4 && height == 4);
        const u8 ci_first[] = {255, 0, 0, 255, 0, 255, 0, 255};
        const u8 rgba_first[] = {0, 0, 255, 255, 0, 0, 255, 255};
        assert(memcmp(rgba, textures % 2 == 0 ? ci_first : rgba_first, 8) == 0);
    } else if (check_wsc3) {
        assert(width == 16 && height == 16 && rgba);
    } else if (check_diagnostic) {
        assert(width == 16 && height == 16);
        if (textures % 2 == 0) {
            assert(memcmp(rgba, (u8[]){0, 0, 123, 255}, 4) == 0);
        } else {
            assert(memcmp(rgba, (u8[]){255, 255, 255, 32}, 4) == 0);
            assert(memcmp(rgba + 8, (u8[]){255, 255, 255, 120}, 4) == 0);
        }
    } else if (check_materials) {
        static const u8 expected[] = {
            255, 0, 0, 255, 0, 255, 0, 0, 66, 66, 66, 255, 255, 0, 0, 255,
            0, 255, 0, 0, 255, 0, 0, 255, 66, 66, 66, 255, 0, 255, 0, 0,
        };
        assert(width == 4 && height == 2 && memcmp(rgba, expected, sizeof(expected)) == 0);
    } else {
        assert(width == 4 && height == 1);
        assert(rgba[0] == 12 && rgba[1] == 34 && rgba[2] == 56 && rgba[3] == 255);
    }
    textures++;
}
static void draw(float *data, size_t length, size_t count) {
    assert(((check_diagnostic || check_wsc3 || check_wsc4 || check_wsc5 || check_scene_probe || check_import)
            ? count > 0 : count == 1) && length > 0);
    for (size_t i = 0; i < length; ++i) assert(isfinite(data[i]));
    if (check_scene_probe) {
        struct CCFeatures features;
        char msl[8192];
        assert(gfx_metal_build_shader(rendering_state.shader_program->id, msl, sizeof(msl), &features));
        assert(features.scene_material);
        ++scene_probe_draws;
    }
    if (check_materials) {
        struct CCFeatures features;
        char msl[8192];
        assert(gfx_metal_build_shader(rendering_state.shader_program->id, msl, sizeof(msl), &features));
        assert(rendering_state.depth_test);
        assert(rendering_state.depth_mask == (triangles == 0));
        assert(rendering_state.alpha_blend == (triangles != 0));
        assert(features.used_textures[0] == (triangles < 2));
        if (triangles == 0) {
            assert(data[4] == 0 && data[5] == 0);
        } else if (triangles == 1) {
            assert(features.opt_alpha && features.color_alpha_same);
            assert(strstr(msl, "float4 color=texel0 * in.input1;"));
            assert(data[4] == 0.75f && data[5] == -0.75f);
            assert(fabsf(data[7] - 128.0f / 255.0f) < 1e-6f);
            assert(data[8] == 0 && fabsf(data[9] - 48.0f / 255.0f) < 1e-6f);
        } else {
            assert(triangles == 2 && data[4] == 1 && data[5] == 1 && data[6] == 1);
            assert(fabsf(data[7] - 128.0f / 255.0f) < 1e-6f);
        }
    }
    if (check_import) {
        struct CCFeatures features;
        gfx_cc_get_features(rendering_state.shader_program->id, &features);
        assert(features.scene_material && rendering_state.depth_test);
        if (!features.scene_blend) {
            assert(features.used_textures[0] && features.used_textures[1] && count == 2);
            assert(bound_s[0] == G_TX_MIRROR && bound_t[0] == G_TX_CLAMP);
            assert(bound_s[1] == G_TX_WRAP && bound_t[1] == G_TX_WRAP);
            assert(fabsf(data[4] - (-32.0f / 128.0f)) < 1e-6f);
            assert(fabsf(data[6] - (-48.0f / 128.0f)) < 1e-6f);
            assert(fabsf(data[19] - 128.0f / 255.0f) < 1e-6f);
        } else {
            assert(!features.used_textures[0] && !features.used_textures[1] && count == 1);
            assert(fabsf(data[7] - 128.0f / 255.0f) < 1e-6f);
        }
    }
    if (check_wsc3) {
        struct CCFeatures features;
        char msl[8192];
        assert(gfx_metal_build_shader(rendering_state.shader_program->id, msl, sizeof(msl), &features));
        assert(features.scene_material && rendering_state.depth_test);
        assert(length == count * 3 * (features.used_textures[1] ? 20 : 16));
        assert(rendering_state.depth_mask != rendering_state.alpha_blend);
        if (features.used_textures[1]) {
            assert(features.used_textures[0] && bound_ids[0] && bound_ids[1] && bound_ids[0] != bound_ids[1]);
            assert(bound_s[0] == G_TX_WRAP && bound_t[0] == G_TX_CLAMP);
            assert(bound_s[1] == G_TX_MIRROR && bound_t[1] == G_TX_WRAP);
            assert(strstr(msl, "sampleTex(tex1,samp1,in.uv1"));
            assert(strstr(msl, "mix(texel0,texel1,in.input3.a)"));
            assert(features.opt_texture_edge == (wsc3_draws == 1));
            assert(features.scene_blend == (wsc3_draws == 2));
            if (!wsc3_draws) {
                memcpy(wsc3_first_uv, data + 4, sizeof(wsc3_first_uv));
                wsc3_first_alpha = data[19];
            }
            ++wsc3_draws;
        }
    }
    if (check_wsc4) {
        struct CCFeatures features;
        char msl[8192];
        assert(gfx_metal_build_shader(rendering_state.shader_program->id, msl, sizeof(msl), &features));
        assert(features.scene_material && features.opt_fog && rendering_state.depth_test);
        assert(strstr(msl, "mix(color.rgb,in.fog.rgb,in.fog.a)"));
        assert(length == count * 3 * 20);
        assert(fabsf(data[7] - 17.92f / 255.0f) < 1e-6f);
        ++wsc4_draws;
    }
    if (check_wsc5) {
        struct CCFeatures features;
        char msl[8192];
        assert(gfx_metal_build_shader(rendering_state.shader_program->id, msl, sizeof(msl), &features));
        assert(features.scene_material && rendering_state.depth_test);
        assert(!rendering_state.depth_mask && rendering_state.alpha_blend);
        assert(rendering_state.decal_mode);
        ++wsc5_draws;
    }
    if (check_diagnostic) {
        assert(rendering_state.depth_test);
        assert(rendering_state.depth_mask != rendering_state.alpha_blend);
        if (rendering_state.alpha_blend) translucent_triangles += count;
    }
    triangles += count;
}

static struct GfxRenderingAPI recorder = {
    .z_is_from_0_to_1 = depth_range, .lookup_shader = lookup,
    .create_and_load_new_shader = create, .load_shader = bind_shader, .unload_shader = bind_shader,
    .shader_get_info = shader_info, .new_texture = new_texture, .select_texture = select_texture,
    .upload_texture = upload, .set_sampler_parameters = sampler, .set_depth_test = toggle,
    .set_depth_mask = toggle, .set_zmode_decal = toggle, .set_use_alpha = toggle,
    .set_viewport = rectangle, .set_scissor = rectangle, .draw_triangles = draw,
    .supports_scene_materials = true,
};

static void prepare_projection(void) {
    const float projection[4][4] = {
        {0.001f,0,0,0},{0,0,0,0},{0,-0.001f,0,0},{0,0,0,1}
    };
    const float matrix[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    gfx_rapi = &recorder;
    gfx_current_dimensions.width = 320;
    gfx_current_dimensions.height = 240;
    gfx_current_dimensions.aspect_ratio = 4.0f / 3;
    gfx_sp_reset();
    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD, (const int32_t *)projection);
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD, (const int32_t *)matrix);
    rsp.geometry_mode = G_ZBUFFER | G_FOG;
    rsp.texture_scaling_factor.s = 17;
    rdp.combine_mode = 0x1234;
}

static void replay_first_room(void) {
    struct WorldRegionInfo region;
    assert(world_region_info_at(0, &region) && region.active);
    assert(region.renderList && !region.translucentList);
    Gfx frame[] = {gsSPDisplayList(NULL), gsSPEndDisplayList()};
    frame[0].words.w1 = (uintptr_t)region.renderList;
    prepare_projection();
    gfx_run_dl(frame);
    assert(rsp.geometry_mode == (G_ZBUFFER | G_FOG));
    assert(rsp.texture_scaling_factor.s == 17);
    assert(rdp.combine_mode == 0x1234);
    assert(buf_vbo_len == 0);
    assert(triangles == 1 && textures == 1);
}

static void check_wsc2_rendering(struct WorldSceneRuntime *runtime) {
    assert(world_scene_open(runtime, 21, 0, NULL) == WORLD_SCENE_OK);
    struct WorldRegionInfo region;
    assert(world_region_info_at(0, &region) && region.active);
    assert(region.renderList && region.translucentList);
    Gfx frame[] = {gsSPDisplayList(NULL), gsSPDisplayList(NULL), gsSPEndDisplayList()};
    frame[0].words.w1 = (uintptr_t)region.renderList;
    frame[1].words.w1 = (uintptr_t)region.translucentList;
    prepare_projection();
    rdp.other_mode_h = G_TF_POINT;
    scene_render_frame = 3;
    check_materials = true;
    triangles = textures = 0;
    gfx_run_dl(frame);
    assert(triangles == 3 && textures == 1);
    assert(!scene_draw_state && !inside_scene_display_list);
    assert(rsp.geometry_mode == (G_ZBUFFER | G_FOG) && rdp.combine_mode == 0x1234);
    assert(world_scene_transition(runtime, 22, 0, NULL) == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_transition(runtime, 23, 0, NULL) == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_active_id(runtime) == 21 && world_region_count() == 1);
    struct Surface *floor;
    assert(find_floor(-150, 80, -100, &floor) == 0 && floor);
    triangles = 0;
    gfx_run_dl(frame);
    assert(triangles == 3 && textures == 1);
    assert(world_scene_transition(runtime, 21, 1, NULL) == WORLD_SCENE_OK);
    for (u32 i = 0; i < gfx_texture_cache.pool_pos; ++i) {
        assert(strcmp((const char *)gfx_texture_cache.pool[i].texture_addr, "scenes/indexed.rgba16") != 0);
    }
    assert(world_scene_transition(runtime, 21, 0, NULL) == WORLD_SCENE_OK);
    assert(world_region_info_at(0, &region) && region.active);
    frame[0].words.w1 = (uintptr_t)region.renderList;
    frame[1].words.w1 = (uintptr_t)region.translucentList;
    triangles = 0;
    gfx_run_dl(frame);
    assert(triangles == 3 && textures == 2);
    world_scene_close(runtime);
    assert(!world_region_count());
    check_materials = false;
}

static void check_area_hooks(void) {
    static struct GraphNodeRoot root;
    static struct SpawnInfo spawn;
    static s16 terrain;
    gObjParentGraphNode.children = &inactive_object.header.gfx.node;
    inactive_object.header.gfx.node.next = gObjParentGraphNode.children;
    gCurrLevelNum = 2;
    gAreaData[1] = (struct Area){.index = 1, .unk04 = &root, .terrainData = &terrain,
                                 .objectSpawnInfos = &spawn};
    struct Surface *floor;
    load_area(1);
    assert(area_scene_package_id() == 513 && find_floor(450, 80, -100, &floor) == 0 && floor);
    unload_area();
    assert(gCurrentArea == NULL && area_scene_package_id() == 0 && world_region_count() == 0);
    assert(find_floor(450, 80, -100, &floor) == -11000.0f && !floor);
}

static void check_bundled_diagnostic(void) {
    static struct GraphNodeRoot root;
    static s16 terrain;
    const float projection[4][4] = {
        {0.0006f, 0, 0, 0}, {0, 0.001f, 0, 0}, {0, -0.0004f, -0.0003f, 0}, {0, 0, 0.6f, 1},
    };
    gCurrLevelNum = LEVEL_CASTLE_GROUNDS;
    gAreaData[SCENE_DIAGNOSTIC_AREA] = (struct Area){.index = SCENE_DIAGNOSTIC_AREA,
        .unk04 = &root, .terrainData = &terrain};
    check_diagnostic = true;
    textures = 0;
    for (int visit = 0; visit < 2; ++visit) {
        load_area(SCENE_DIAGNOSTIC_AREA);
        assert(gCurrAreaIndex == SCENE_DIAGNOSTIC_AREA && area_scene_package_id() == SCENE_DIAGNOSTIC_PACKAGE);
        struct Surface *floor;
        assert(find_floor(0, 40, 720, &floor) == 0 && floor);
        assert(find_floor(900, 180, 60, &floor) == 72 && floor && floor->normal.y > 0.95f);
        assert(find_floor(900, 180, -400, &floor) == 144 && floor);
        scene_diagnostic_check_failed_load();
        assert(area_scene_package_id() == SCENE_DIAGNOSTIC_PACKAGE && world_region_count() == 1);
        assert(find_floor(0, 40, 720, &floor) == 0 && floor);
        struct WorldRegionInfo region;
        assert(world_region_info_at(0, &region) && region.active && region.renderList && region.translucentList);
        Gfx frame[] = {gsSPDisplayList(NULL), gsSPDisplayList(NULL), gsSPEndDisplayList()};
        frame[0].words.w1 = (uintptr_t)region.renderList;
        frame[1].words.w1 = (uintptr_t)region.translucentList;
        prepare_projection();
        gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD, (const int32_t *)projection);
        triangles = translucent_triangles = 0;
        gfx_run_dl(frame);
        assert(triangles == 74 && translucent_triangles == 2 && textures == (visit + 1) * 2u);
        unload_area();
        assert(area_scene_package_id() == 0 && world_region_count() == 0);
        assert(find_floor(0, 40, 720, &floor) == -11000.0f && !floor);
    }
    check_diagnostic = false;
}

static void check_wsc3_diagnostic(void) {
    static struct GraphNodeRoot root;
    static s16 terrain;
    const float projection[4][4] = {
        {0.0006f, 0, 0, 0}, {0, 0.001f, 0, 0}, {0, -0.0004f, -0.0003f, 0}, {0, 0, 0.6f, 1},
    };
    gCurrLevelNum = LEVEL_CASTLE_GROUNDS;
    gAreaData[SCENE_DIAGNOSTIC_WSC3_AREA] = (struct Area){.index = SCENE_DIAGNOSTIC_WSC3_AREA,
        .unk04 = &root, .terrainData = &terrain};
    check_wsc3 = true;
    textures = 0;
    for (int visit = 0; visit < 2; ++visit) {
        load_area(SCENE_DIAGNOSTIC_WSC3_AREA);
        assert(area_scene_package_id() == SCENE_DIAGNOSTIC_WSC3_PACKAGE);
        struct Surface *floor;
        assert(find_floor(0, 40, 720, &floor) == 0 && floor);
        struct WorldRegionInfo region;
        assert(world_region_info_at(0, &region) && region.active && region.renderList && region.translucentList);
        Gfx frame[] = {gsSPDisplayList(NULL), gsSPDisplayList(NULL), gsSPEndDisplayList()};
        frame[0].words.w1 = (uintptr_t)region.renderList;
        frame[1].words.w1 = (uintptr_t)region.translucentList;
        prepare_projection();
        gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD, (const int32_t *)projection);
        float previous_uv[4];
        for (int step = 0; step < 2; ++step) {
            scene_render_frame = step * 90;
            triangles = wsc3_draws = 0;
            gfx_run_dl(frame);
            assert(triangles == 72 && wsc3_draws == 3);
            assert(textures == (visit + 1) * 2u);
            assert(!scene_material && !scene_bound_material && !inside_scene_display_list);
            assert(rsp.geometry_mode == (G_ZBUFFER | G_FOG) && rdp.combine_mode == 0x1234);
            assert(fabsf(wsc3_first_alpha - (step == 0 ? 24.0f : 233.0f) / 255.0f) < 1e-6f);
            if (step == 0) {
                memcpy(previous_uv, wsc3_first_uv, sizeof(previous_uv));
            } else {
                assert(wsc3_first_uv[0] != previous_uv[0] && wsc3_first_uv[1] == previous_uv[1]);
                assert(wsc3_first_uv[2] != previous_uv[2] && wsc3_first_uv[3] != previous_uv[3]);
            }
        }
        unload_area();
        assert(area_scene_package_id() == 0 && world_region_count() == 0);
        assert(find_floor(0, 40, 720, &floor) == -11000.0f && !floor);
    }
    check_wsc3 = false;
}

static void check_wsc4_fog(struct WorldSceneRuntime *runtime) {
    const float projection[4][4] = {
        {0.001f, 0, 0, 0}, {0, 0.001f, 0, 0}, {0, 0, 0.0001f, 0}, {0, 0, 0, 1},
    };
    struct WorldSceneDestination destination;
    assert(world_scene_open(runtime, 25, 0, &destination) == WORLD_SCENE_OK);
    struct WorldRegionInfo region;
    assert(world_region_info_at(0, &region) && region.active && region.renderList);
    Gfx frame[] = {gsSPDisplayList(NULL), gsSPEndDisplayList()};
    frame[0].words.w1 = (uintptr_t)region.renderList;
    check_wsc4 = true;
    triangles = wsc4_draws = 0;
    prepare_projection();
    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD, (const int32_t *)projection);
    gfx_run_dl(frame);
    assert(triangles > 0 && wsc4_draws > 0);
    check_wsc4 = false;
    world_scene_close(runtime);
    assert(!world_region_count());
}

static void check_wsc5_decal(struct WorldSceneRuntime *runtime) {
    const float projection[4][4] = {
        {0.001f, 0, 0, 0}, {0, 0.001f, 0, 0}, {0, 0, 0.0001f, 0}, {0, 0, 0, 1},
    };
    assert(world_scene_open(runtime, 26, 0, NULL) == WORLD_SCENE_OK);
    struct WorldRegionInfo region;
    assert(world_region_info_at(0, &region) && region.active && region.renderList);
    Gfx frame[] = {gsSPDisplayList(NULL), gsSPEndDisplayList()};
    frame[0].words.w1 = (uintptr_t)region.renderList;
    check_wsc5 = true;
    triangles = wsc5_draws = 0;
    prepare_projection();
    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD, (const int32_t *)projection);
    gfx_run_dl(frame);
    assert(triangles > 0 && wsc5_draws > 0);
    check_wsc5 = false;
    world_scene_close(runtime);
    assert(!world_region_count());
    assert(world_scene_open(runtime, 27, 0, NULL) == WORLD_SCENE_PROVIDER_FAILED);
    assert(!world_region_count());
}

static void check_wsc5_many_batches(struct WorldSceneRuntime *runtime) {
    assert(world_scene_open(runtime, 28, 0, NULL) == WORLD_SCENE_OK);
    struct WorldRegionInfo region;
    assert(world_region_info_at(0, &region) && region.active && region.renderList);
    world_scene_close(runtime);
    assert(!world_region_count());
}

static void check_wsc5_texture_limit(struct WorldSceneRuntime *runtime) {
    assert(world_scene_open(runtime, 29, 0, NULL) == WORLD_SCENE_OK);
    world_scene_close(runtime);
    assert(!world_region_count());
    assert(world_scene_open(runtime, 30, 0, NULL) == WORLD_SCENE_PROVIDER_FAILED);
    assert(!world_region_count());
}

static void check_external_scene_probe(struct WorldSceneRuntime *runtime, u32 sceneId,
                                       u32 minimumTriangles, u32 minimumUploads,
                                       u32 expectedCollisionTriangles) {
    struct WorldSceneDestination destination;
    assert(world_scene_open(runtime, sceneId, 0, &destination) == WORLD_SCENE_OK);
    assert(world_region_count() == 1);
    struct WorldRegionInfo region;
    assert(world_region_info_at(0, &region) && region.active && region.renderList);
    assert(region.triangleCount == expectedCollisionTriangles);
    assert(destination.position.x >= region.min.x && destination.position.x <= region.max.x);
    assert(destination.position.y >= region.min.y && destination.position.y <= region.max.y);
    assert(destination.position.z >= region.min.z && destination.position.z <= region.max.z);
    struct Surface *floor = NULL;
    f32 floorY = find_floor((f32)destination.position.x, (f32)destination.position.y + 200.0f,
                            (f32)destination.position.z, &floor);
    assert(floor && floorY == (f32)destination.position.y - 16.0f);

    prepare_projection();
    check_scene_probe = true;
    triangles = textures = scene_probe_draws = scene_probe_uploads = 0;
    if (region.translucentList) {
        Gfx frame[] = {gsSPDisplayList(NULL), gsSPDisplayList(NULL), gsSPEndDisplayList()};
        frame[0].words.w1 = (uintptr_t)region.renderList;
        frame[1].words.w1 = (uintptr_t)region.translucentList;
        gfx_run_dl(frame);
    } else {
        Gfx frame[] = {gsSPDisplayList(NULL), gsSPEndDisplayList()};
        frame[0].words.w1 = (uintptr_t)region.renderList;
        gfx_run_dl(frame);
    }
    assert(triangles >= minimumTriangles && scene_probe_draws > 0
           && scene_probe_uploads >= minimumUploads);
    printf("WSC scene %u host probe: submitted-triangles=%u draw-calls=%u texture-uploads=%u collision-triangles=%u spawn=(%lld,%lld,%lld) room-bounds=(%lld,%lld,%lld)..(%lld,%lld,%lld) floor=%.1f\n",
           sceneId, triangles, scene_probe_draws, scene_probe_uploads, region.triangleCount,
           (long long)destination.position.x, (long long)destination.position.y,
           (long long)destination.position.z, (long long)region.min.x,
           (long long)region.min.y, (long long)region.min.z, (long long)region.max.x,
           (long long)region.max.y, (long long)region.max.z, floorY);
    check_scene_probe = false;
    world_scene_close(runtime);
    assert(world_region_count() == 0);

    /* Exercise the production area-selection, package activation and spawn
     * clearance path as well as the lower-level package reader. */
    static struct GraphNodeRoot root;
    static s16 terrain;
    gObjParentGraphNode.children = &inactive_object.header.gfx.node;
    inactive_object.header.gfx.node.next = gObjParentGraphNode.children;
    gCurrLevelNum = LEVEL_CASTLE_GROUNDS;
    gAreaData[SCENE_EXTERNAL_AREA] = (struct Area){.index = SCENE_EXTERNAL_AREA,
        .unk04 = &root, .terrainData = &terrain};
    world_region_reset((struct WorldPoint){0, 0, 0});
    assert(area_external_scene_available(sceneId));
    assert(area_external_scene_select(sceneId));
    load_area(SCENE_EXTERNAL_AREA);
    assert(area_external_scene_is_active() && area_scene_package_id() == sceneId);
    struct SpawnInfo spawn = {0};
    assert(area_external_scene_spawn(&spawn));
    assert(spawn.startPos[0] == destination.position.x
           && spawn.startPos[1] == destination.position.y
           && spawn.startPos[2] == destination.position.z);
    area_external_scene_track((f32[]){destination.position.x, destination.position.y,
                                       destination.position.z});
    unload_area();
    assert(!area_external_scene_is_active() && world_region_count() == 0);
}

static void check_external_scene_switching(void) {
    static struct GraphNodeRoot root;
    static s16 terrain;
    gObjParentGraphNode.children = &inactive_object.header.gfx.node;
    inactive_object.header.gfx.node.next = gObjParentGraphNode.children;
    gCurrLevelNum = LEVEL_CASTLE_GROUNDS;
    gAreaData[SCENE_EXTERNAL_AREA] = (struct Area){.index = SCENE_EXTERNAL_AREA,
        .unk04 = &root, .terrainData = &terrain};
    world_region_reset((struct WorldPoint){0, 0, 0});
    assert(area_external_scene_available(SCENE_EXTERNAL_PACKAGE));
    assert(area_external_scene_available(SCENE_EXTERNAL_PACKAGE_MARKET_DAY));
    assert(area_external_scene_select(SCENE_EXTERNAL_PACKAGE));
    load_area(SCENE_EXTERNAL_AREA);
    assert(area_external_scene_is_active() && area_scene_package_id() == SCENE_EXTERNAL_PACKAGE);

    /* Selection occurs before the normal SM64 area unload/reload boundary, so
     * the prior scene ID is preserved while its resources are released. */
    assert(area_external_scene_select(SCENE_EXTERNAL_PACKAGE_MARKET_DAY));
    assert(area_external_scene_previous_id() == SCENE_EXTERNAL_PACKAGE);
    unload_area();
    assert(world_region_count() == 0 && !area_external_scene_is_active());
    load_area(SCENE_EXTERNAL_AREA);
    assert(area_external_scene_is_active()
           && area_scene_package_id() == SCENE_EXTERNAL_PACKAGE_MARKET_DAY);
    struct SpawnInfo spawn = {0};
    assert(area_external_scene_spawn(&spawn));
    assert(spawn.startPos[0] == -9 && spawn.startPos[1] == 16 && spawn.startPos[2] == 1616);

    u32 previous = area_external_scene_previous_id();
    assert(previous == SCENE_EXTERNAL_PACKAGE);
    assert(area_external_scene_select(previous));
    assert(area_external_scene_previous_id() == SCENE_EXTERNAL_PACKAGE_MARKET_DAY);
    unload_area();
    load_area(SCENE_EXTERNAL_AREA);
    assert(area_external_scene_is_active() && area_scene_package_id() == SCENE_EXTERNAL_PACKAGE);
    unload_area();
    assert(world_region_count() == 0 && !area_external_scene_is_active());
}

static void check_imported_scene(const char *texture_path) {
    static struct GraphNodeRoot root;
    static s16 terrain;
    const float projection[4][4] = {
        {0.001f, 0, 0, 0}, {0, 0.001f, 0, 0}, {0, -0.001f, 0, 0}, {0, 0, 0, 1},
    };
    gCurrLevelNum = LEVEL_CASTLE_GROUNDS;
    gAreaData[SCENE_EXTERNAL_AREA] = (struct Area){.index = SCENE_EXTERNAL_AREA,
        .unk04 = &root, .terrainData = &terrain};
    check_import = true;
    textures = 0;
    assert(area_external_scene_available(SCENE_EXTERNAL_PACKAGE));
    assert(area_external_scene_select(SCENE_EXTERNAL_PACKAGE));
    for (int visit = 0; visit < 2; ++visit) {
        load_area(SCENE_EXTERNAL_AREA);
        assert(area_scene_package_id() == SCENE_EXTERNAL_PACKAGE && world_region_count() == 2);
        struct SpawnInfo spawn = {0};
        assert(area_external_scene_spawn(&spawn));
        assert(spawn.startPos[0] == -180 && spawn.startPos[1] == 16 && spawn.startPos[2] == 0);
        for (int direction = -1; direction <= 1; direction += 2) {
            for (int x = -180; x <= 180; x += 10) {
                struct Surface *floor;
                assert(find_floor(x * direction, 16, 0, &floor) == 0 && floor);
                area_external_scene_track((f32[]){x * direction, 16, 0});
                assert(world_region_count() == 2);
            }
        }
        triangles = 0;
        for (u32 i = 0; i < 2; ++i) {
            struct WorldRegionInfo region;
            assert(world_region_info_at(i, &region) && region.active && region.translucentList);
            Gfx frame[] = {gsSPDisplayList(NULL), gsSPDisplayList(NULL), gsSPEndDisplayList()};
            frame[0].words.w1 = (uintptr_t)region.renderList;
            frame[1].words.w1 = (uintptr_t)region.translucentList;
            prepare_projection();
            gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD, (const int32_t *)projection);
            gfx_run_dl(frame);
        }
        assert(triangles == 6 && textures == (unsigned)(visit + 1) * 2u);
        unload_area();
        assert(!world_region_count() && !area_external_scene_spawn(NULL));
    }
    assert(rename(texture_path, "hidden-texture.png") == 0);
    load_area(SCENE_EXTERNAL_AREA);
    assert(!area_external_scene_spawn(NULL) && !world_region_count());
    unload_area();
    assert(rename("hidden-texture.png", texture_path) == 0);
    load_area(SCENE_EXTERNAL_AREA);
    assert(area_external_scene_spawn(NULL) && world_region_count() == 2);
    unload_area();
    assert(!world_region_count());

    const char *path = fs_get_write_path("scenes/4100.wsc");
    FILE *file = fopen(path, "r+b");
    assert(file && fputc('X', file) != EOF && fclose(file) == 0);
    assert(area_external_scene_available(SCENE_EXTERNAL_PACKAGE));
    load_area(SCENE_EXTERNAL_AREA);
    assert(!area_external_scene_spawn(NULL) && !world_region_count());
    unload_area();

    path = fs_get_write_path("scenes/4100.wsc");
    assert(remove(path) == 0 && !area_external_scene_available(SCENE_EXTERNAL_PACKAGE));
    load_area(SCENE_EXTERNAL_AREA);
    assert(!area_external_scene_spawn(NULL) && !world_region_count());
    unload_area();
    check_import = false;
}

int main(int argc, char **argv) {
    const char *rodirs[] = { NULL };
    if (argc == 3 && !strcmp(argv[1], "--probe-wsc5-scenes")) {
        assert(fs_init(rodirs, argv[2], argv[2]));
        prepare_projection();
        check_external_scene_switching();
        puts("Imported WSC scene selection, spawn validation, switch, return, reload and teardown passed");
        return 0;
    }
    if (argc == 7 && !strcmp(argv[1], "--probe-wsc5-scene")) {
        assert(fs_init(rodirs, argv[2], argv[2]));
        world_region_reset((struct WorldPoint){0, 0, 0});
        prepare_projection();
        struct WorldSceneRuntime runtime;
        world_scene_runtime_init(&runtime, &gWorldSceneFileProvider, NULL);
        check_external_scene_probe(&runtime, (u32)strtoul(argv[3], NULL, 0),
                                   (u32)strtoul(argv[4], NULL, 0),
                                   (u32)strtoul(argv[5], NULL, 0),
                                   (u32)strtoul(argv[6], NULL, 0));
        world_scene_close(&runtime);
        puts("WSC scene probe load/render/collision/teardown passed");
        return 0;
    }
    assert(argc == 4);
    assert(fs_init(rodirs, argv[1], argv[1]));
    world_region_reset((struct WorldPoint){0, 0, 0});
    struct WorldSceneRuntime runtime;
    struct WorldSceneDestination destination;
    world_scene_runtime_init(&runtime, &gWorldSceneFileProvider, NULL);

    assert(world_scene_open(&runtime, 11, 0, &destination) == WORLD_SCENE_OK);
    assert(destination.roomId == 1 && world_region_count() == 1);
    struct Surface *floor = NULL;
    assert(find_floor(-150, 80, -100, &floor) == 0 && floor);
    assert(world_region_update((struct WorldPoint){3000, 0, 3000}, 0, 0) == WORLD_REGION_OK);
    assert(find_floor(-150, 80, -100, &floor) == 0 && floor);
    replay_first_room();
    assert(world_scene_update_residency(&runtime, (struct WorldPoint){-150, 0, -100},
                                        70, 0) == WORLD_SCENE_OK);
    assert(world_region_count() == 2);
    assert(find_floor(150, 80, -100, &floor) == 0 && floor);
    assert(world_scene_transition(&runtime, 11, 1, &destination) == WORLD_SCENE_OK);
    assert(world_region_count() == 1 && destination.roomId == 2);
    assert(find_floor(-150, 80, -100, &floor) == -11000.0f && !floor);

    assert(world_scene_transition(&runtime, 13, 0, &destination) == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_transition(&runtime, 14, 0, &destination) == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_transition(&runtime, 24, 0, &destination) == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_transition(&runtime, 27, 0, &destination) == WORLD_SCENE_PROVIDER_FAILED);
    assert(world_scene_active_id(&runtime) == 11 && world_region_count() == 1);
    assert(find_floor(150, 80, -100, &floor) == 0 && floor);
    assert(world_scene_traverse_exit(&runtime, 42, &destination) == WORLD_SCENE_OK);
    assert(destination.sceneId == 12 && world_region_count() == 1);
    assert(gfx_texture_cache.pool[0].texture_addr[0] == '\0');
    assert(find_floor(150, 80, -100, &floor) == -11000.0f && !floor);
    assert(find_floor(450, 80, -100, &floor) == 0 && floor);
    assert(world_scene_traverse_exit(&runtime, 43, &destination) == WORLD_SCENE_OK);
    triangles = textures = 0;
    replay_first_room();
    world_scene_close(&runtime);
    assert(world_region_count() == 0);
    assert(find_floor(-150, 80, -100, &floor) == -11000.0f && !floor);
    check_area_hooks();
    check_wsc2_rendering(&runtime);
    assert(fs_mount(argv[2]));
    check_bundled_diagnostic();
    check_wsc3_diagnostic();
    check_wsc4_fog(&runtime);
    check_wsc5_decal(&runtime);
    check_wsc5_many_batches(&runtime);
    check_wsc5_texture_limit(&runtime);
    check_imported_scene(argv[3]);
    recorder.supports_scene_materials = false;
    assert(world_scene_open(&runtime, SCENE_DIAGNOSTIC_WSC3_PACKAGE, 0, NULL) == WORLD_SCENE_PROVIDER_FAILED);
    assert(!world_region_count());
    puts("Scene packages: render, texture upload, collision, room transitions and rollback passed");
    return 0;
}
