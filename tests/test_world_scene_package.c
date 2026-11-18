/* Host replay through the production decoder, Gfx interpreter, and partition. */
#include <assert.h>
#include "src/pc/gfx/gfx_pc.c"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "game/area.h"
#include "game/object_list_processor.h"

SpatialPartitionCell gStaticSurfacePartition[SURFACE_PARTITION_DIM][SURFACE_PARTITION_DIM];
SpatialPartitionCell gDynamicSurfacePartition[SURFACE_PARTITION_DIM][SURFACE_PARTITION_DIM];
struct NumTimesCalled gNumCalls;
struct Object *gCurrentObject;
struct Object *gMarioObject;
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
    assert(index == 1 && data && !rooms && !macroObjects);
    assert(world_region_count() == 0);
    base_terrain_loaded = true;
}

void spawn_objects_from_info(s32 index, struct SpawnInfo *info) {
    assert(index == 0 && info && base_terrain_loaded);
    assert(area_scene_package_id() == 513 && world_region_count() == 1);
}

void unload_objects_from_area(s32 index, s32 area) {
    assert(index == 0 && area == 1 && world_region_count() == 1);
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
static struct ShaderProgram shader;
static unsigned triangles, textures;

static struct ShaderProgram *lookup(u32 id) { return shader.id == id ? &shader : NULL; }
static struct ShaderProgram *create(u32 id) { shader.id = id; return &shader; }
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
static void select_texture(int tile, u32 id) { assert(tile == 0 && id > 0); }
static void sampler(int tile, bool linear, u32 s, u32 t) {
    assert(tile == 0);
    (void)linear; (void)s; (void)t;
}
static void upload(const u8 *rgba, int width, int height) {
    assert(width == 4 && height == 1);
    assert(rgba[0] == 12 && rgba[1] == 34 && rgba[2] == 56 && rgba[3] == 255);
    textures++;
}
static void draw(float *data, size_t length, size_t count) {
    assert(count == 1 && length > 0);
    for (size_t i = 0; i < length; ++i) assert(isfinite(data[i]));
    triangles += count;
}

static struct GfxRenderingAPI recorder = {
    .z_is_from_0_to_1 = depth_range, .lookup_shader = lookup,
    .create_and_load_new_shader = create, .load_shader = bind_shader, .unload_shader = bind_shader,
    .shader_get_info = shader_info, .new_texture = new_texture, .select_texture = select_texture,
    .upload_texture = upload, .set_sampler_parameters = sampler, .set_depth_test = toggle,
    .set_depth_mask = toggle, .set_zmode_decal = toggle, .set_use_alpha = toggle,
    .set_viewport = rectangle, .set_scissor = rectangle, .draw_triangles = draw,
};

static void replay_first_room(void) {
    struct WorldRegionInfo region;
    assert(world_region_info_at(0, &region) && region.active);
    assert(region.renderList);
    Gfx frame[] = {gsSPDisplayList(NULL), gsSPEndDisplayList()};
    frame[0].words.w1 = (uintptr_t)region.renderList;
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
    gfx_run_dl(frame);
    assert(rsp.geometry_mode == (G_ZBUFFER | G_FOG));
    assert(rsp.texture_scaling_factor.s == 17);
    assert(rdp.combine_mode == 0x1234);
    assert(buf_vbo_len == 0);
    assert(triangles == 1 && textures == 1);
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

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *rodirs[] = { NULL };
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
    puts("Scene packages: render, texture upload, collision, room transitions and rollback passed");
    return 0;
}
