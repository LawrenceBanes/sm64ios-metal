/* Run the production display-list interpreter up to the graphics API boundary.
 * The test mounts the generated base pack and records draws and texture uploads. */
#include "src/pc/gfx/gfx_pc.c"
#include "pc/mods/dynos_actor.h"
#include "pc/mods/dynos_actor_native.h"

static bool sDynosReplay;
static struct {
    char key[32];
    uint8_t *pixels;
    uint32_t width, height;
} sDynosTextures[256];
static size_t sDynosTextureCount;

static const void *dynos_test_texture(void *context, const char *name,
                                      const uint8_t *png, size_t size) {
    (void)context;
    (void)name;
    if (sDynosTextureCount >= 256 || size > INT_MAX) return NULL;
    int width, height;
    uint8_t *pixels = stbi_load_from_memory(png, (int)size, &width, &height, NULL, 4);
    if (!pixels || width <= 0 || height <= 0 || width > 512 || height > 512) {
        stbi_image_free(pixels);
        return NULL;
    }
    size_t index = sDynosTextureCount++;
    snprintf(sDynosTextures[index].key, sizeof(sDynosTextures[index].key),
             "m69tex:actor%zu", index);
    sDynosTextures[index].pixels = pixels;
    sDynosTextures[index].width = (uint32_t)width;
    sDynosTextures[index].height = (uint32_t)height;
    return sDynosTextures[index].key;
}

static void *dynos_test_geo_function(void *node, int context) {
    (void)node;
    (void)context;
    return NULL;
}

static const void *dynos_test_function(void *context, uint32_t index) {
    (void)context;
    /* The renderer replay does not invoke GeoLayout callbacks. */
    return index < 64 ? (const void *)dynos_test_geo_function : NULL;
}

static const uint8_t sDynamicModTextureRgba[] = {
    255, 32, 16, 255, 16, 32, 255, 192,
};
static unsigned sDynamicModTextureUploads;

// This replay harness does not link the mod runtime. Provide one bounded
// decoded texture at its renderer boundary to exercise the production upload
// branch independently from Lua/mod ownership.
bool mod_runtime_dynamic_texture_lookup(const char *key, const uint8_t **rgba,
                                        uint32_t *width, uint32_t *height) {
    for (size_t i = 0; i < sDynosTextureCount; ++i) {
        if (key && !strcmp(key, sDynosTextures[i].key)) {
            *rgba = sDynosTextures[i].pixels;
            *width = sDynosTextures[i].width;
            *height = sDynosTextures[i].height;
            return true;
        }
    }
    if (!key || strcmp(key, "m69tex:host-fixture") != 0 || !rgba || !width || !height)
        return false;
    *rgba = sDynamicModTextureRgba;
    *width = 2;
    *height = 1;
    return true;
}
#include "hyrule_field_runtime.inc.c"
#include "sm64.h"
#include "geo_commands.h"
#include "engine/math_util.h"
#include "engine/graph_node.h"
#ifdef REPLAY_FIXTURE
#define HYRULE_CONNECTION_X (-13200)
#define HYRULE_CONNECTION_Y 260
#define HYRULE_CONNECTION_Z 1460
#define HYRULE_CONNECTION_YAW (-90)
#else
#include "hyrule_field_connection.h"
#endif

unsigned int configFiltering = 1;
static unsigned draws, uploads, texture_ids;
static unsigned missing_hyrule_uploads;
static struct CCFeatures features;

static void record_draw(float *stream, size_t floats, size_t triangles) {
    for (size_t i = 0; i < floats; ++i) assert(isfinite(stream[i]));
#ifdef REPLAY_FIXTURE
    if (!sDynosReplay) {
    // The static water uses the authored primitive opacity (102/255). Check
    // the actual shader input and its alpha in the interleaved vertex stream.
    assert(features.opt_alpha && features.do_single[1]);
    unsigned alpha_input = features.c[1][3];
    assert(alpha_input >= SHADER_INPUT_1 && alpha_input <= SHADER_INPUT_4);
    unsigned input_start = 4 + (features.used_textures[0] || features.used_textures[1] ? 2 : 0)
                             + (features.opt_fog ? 4 : 0);
    unsigned stride = input_start + 4 * features.num_inputs;
    assert(floats == triangles * 3 * stride);
    for (size_t v = 0; v < floats; v += stride) {
        assert(fabsf(stream[v + input_start + (alpha_input - 1) * 4 + 3] - 0.4f) < 1.e-6f);
    }
    }
#endif
    draws += triangles;
}

static void record_upload(const uint8_t *rgba, int width, int height) {
    assert(rgba != missing_texture && width > 0 && height > 0);
    for (size_t i = 0; i < sDynosTextureCount; ++i) {
        if (rgba == sDynosTextures[i].pixels) {
            assert(width == (int)sDynosTextures[i].width &&
                   height == (int)sDynosTextures[i].height);
            ++uploads;
            return;
        }
    }
    if (rgba == sDynamicModTextureRgba) {
        static const uint8_t expected[] = {255, 32, 16, 255, 16, 32, 255, 192};
        assert(width == 2 && height == 1 && !memcmp(rgba, expected, sizeof(expected)));
        ++sDynamicModTextureUploads;
        ++uploads;
        return;
    }
    if (rgba == missing_hyrule_texture) {
        assert(getenv("REPLAY_EXPECT_MISSING_HYRULE_TEXTURE"));
        assert(width == 2 && height == 2);
        for (size_t i = 3; i < sizeof(missing_hyrule_texture); i += 4)
            assert(rgba[i] == 255);
        ++missing_hyrule_uploads;
        ++uploads;
        return;
    }
#ifdef REPLAY_FIXTURE
    assert(!sDynosReplay);
    const uint8_t expected[] = {255,0,0,255, 0,255,0,255};
    assert(width == 1 && height == 2 && !memcmp(rgba, expected, sizeof(expected)));
#endif
    ++uploads;
}

struct ShaderProgram {
    uint32_t id;
};
static struct ShaderProgram programs[64];
static size_t program_count;

static struct ShaderProgram *lookup_shader(uint32_t id) {
    for (size_t i = 0; i < program_count; ++i) {
        if (programs[i].id == id) return &programs[i];
    }
    return NULL;
}

static struct ShaderProgram *new_shader(uint32_t id) {
    assert(program_count < sizeof(programs) / sizeof(programs[0]));
    programs[program_count].id = id;
    return &programs[program_count++];
}

static void shader_info(struct ShaderProgram *program, uint8_t *inputs, bool textures[2]) {
    gfx_cc_get_features(program->id, &features);
    *inputs = features.num_inputs;
    memcpy(textures, features.used_textures, sizeof(features.used_textures));
}

static void bind_shader(struct ShaderProgram *program) { (void)program; }
static bool metal_clip_depth(void) { return true; }
static uint32_t new_texture(void) { return ++texture_ids; }
static void select_texture(int tile, uint32_t id) { assert(tile < 2 && id > 0); }
static void sampler(int tile, bool linear, uint32_t s, uint32_t t) {}
static void toggle(bool enabled) {}
static void rect(int x, int y, int w, int h) {}

static struct GfxRenderingAPI recorder = {
    .z_is_from_0_to_1 = metal_clip_depth, .lookup_shader = lookup_shader,
    .create_and_load_new_shader = new_shader,
    .load_shader = bind_shader, .unload_shader = bind_shader, .shader_get_info = shader_info,
    .new_texture = new_texture, .select_texture = select_texture, .upload_texture = record_upload,
    .set_sampler_parameters = sampler, .set_depth_test = toggle, .set_depth_mask = toggle,
    .set_zmode_decal = toggle, .set_use_alpha = toggle, .set_viewport = rect, .set_scissor = rect,
    .draw_triangles = record_draw,
};

static void test_dynamic_mod_texture_upload(void) {
    static const uint8_t texture_key[] = "m69tex:host-fixture";
    rdp.texture_tile.fmt = G_IM_FMT_RGBA;
    rdp.texture_tile.siz = G_IM_SIZ_32b;
    rdp.loaded_texture[0].addr = texture_key;
    rdp.loaded_texture[0].size_bytes = sizeof(sDynamicModTextureRgba);
    import_texture(0);
    assert(sDynamicModTextureUploads == 1);
}

static void replay_dynos_actor(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 16 && length < 8 * 1024 * 1024 && !fseek(file, 0, SEEK_SET));
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    DynosActorBundle bundle;
    DynosNativeActor native;
    char error[400];
    assert(dynos_actor_decode(bytes, (size_t)length, &bundle, error, sizeof(error)));
    assert(dynos_actor_native_build(&bundle, dynos_test_texture,
                                    dynos_test_function, NULL, &native, error, sizeof(error)));
    sDynosReplay = true;
    unsigned old_draws = draws, old_uploads = uploads;
    for (size_t i = 0; i < bundle.record_count; ++i) {
        if (bundle.records[i].type != 4) continue;
        const Gfx *commands = native.resources[i];
        bool textured = false;
        for (uint32_t j = 0; j < bundle.records[i].count; ++j)
            textured |= ((uint8_t)(commands[j].words.w0 >> 24) == G_SETTIMG);
        if (!textured) continue;
        gfx_sp_reset();
        gfx_run_dl((Gfx *)commands);
        gfx_flush();
        if (draws > old_draws && uploads > old_uploads) break;
    }
    assert(draws > old_draws && uploads > old_uploads);
    fprintf(stderr, "DynOS host renderer: %u triangles, %u texture uploads\n",
            draws - old_draws, uploads - old_uploads);
    sDynosReplay = false;
    dynos_actor_native_free(&native);
    dynos_actor_free(&bundle);
    free(bytes);
    for (size_t i = 0; i < sDynosTextureCount; ++i)
        stbi_image_free(sDynosTextures[i].pixels);
    sDynosTextureCount = 0;
}

static void replay_passes(bool connected) {
    const Gfx *passes[] = { hyrule_field_opaque_dl, hyrule_field_translucent_dl };
    const Gfx expected_modes[] = {
        gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2),
        gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2)
    };
    for (size_t i = 0; i < 2; ++i) {
        rsp.geometry_mode = G_FOG | G_CULL_FRONT | G_TEXTURE_GEN;
        rsp.fog_offset = 255;
        gfx_run_dl((Gfx *)passes[i]);
        gfx_flush();
        assert(!(rsp.geometry_mode & (G_FOG | G_CULL_FRONT | G_TEXTURE_GEN)));
        assert(rsp.geometry_mode & G_ZBUFFER);
#ifdef REPLAY_FIXTURE
        // A culling-center offset would move this vertex off screen.
        float x0 = connected ? -13200.f / 20000 : 0;
        float y0 = connected ? (260.f - 1460.f) / 20000 : 0;
        assert(fabsf(rsp.loaded_vertices[0].x - x0) < 1.e-6f);
        assert(fabsf(rsp.loaded_vertices[0].y - y0) < 1.e-6f);
        assert(fabsf(rsp.loaded_vertices[1].x - (connected ? x0 : 10.f / 20000)) < 1.e-6f);
        assert(fabsf(rsp.loaded_vertices[1].y - (connected ? y0 - 10.f / 20000 : 0)) < 1.e-6f);
        assert(draws == (connected ? 2 : 0) + i + 1);
#endif
        assert(rsp.fog_offset == 0 && rsp.fog_mul == 0);
        assert(rdp.other_mode_l == expected_modes[i].words.w1);
    }
}

int main(int argc, char **argv) {
    assert(argc == 2 && fs_mount(argv[1]));
    gfx_rapi = &recorder;
    gfx_current_dimensions.width = 320;
    gfx_current_dimensions.height = 240;
    gfx_sp_reset();
    // Oblique overview of the whole source room. Both sides of the crest fit.
    const float projection[4][4] = {
        {1.f/20000, 0, 0, 0}, {0, 1.f/20000, 1.f/20000, 0},
        {0, -1.f/20000, 0, 0}, {0, 0, 0, 1}
    };
    const float identity[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD, (const int32_t *)projection);
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD, (const int32_t *)identity);
    replay_passes(false);

    // The same 64-bit packed node and matrix helpers used by Castle's GeoLayout.
    static const GeoLayout node[] = {
        GEO_TRANSLATE_ROTATE_WITH_DL(LAYER_OPAQUE, HYRULE_CONNECTION_X, HYRULE_CONNECTION_Y,
                                     HYRULE_CONNECTION_Z, 0, HYRULE_CONNECTION_YAW, 0, hyrule_field_opaque_dl)
    };
    Vec3s position, angles;
    Vec3f translation;
    Mat4 matrix;
    s16 *next = read_vec3s(position, (s16 *)node + 2);
    next = read_vec3s_angle(angles, next);
    assert(*(const Gfx **)next == hyrule_field_opaque_dl);
    vec3s_to_vec3f(translation, position);
    mtxf_rotate_zxy_and_translate(matrix, translation, angles);
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD, (const int32_t *)matrix);
    replay_passes(true);

    assert(draws && uploads && !gfx_texture_cache_failed_binding_count);
    if (getenv("REPLAY_EXPECT_MISSING_HYRULE_TEXTURE"))
        assert(missing_hyrule_uploads > 0);
    else
        assert(missing_hyrule_uploads == 0);
    test_dynamic_mod_texture_upload();
    printf("Hyrule host replay: %u triangles submitted, %u texture uploads\n", draws, uploads);
    if (getenv("DYNOS_REPLAY_BUNDLE")) {
        gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD, (const int32_t *)identity);
        replay_dynos_actor(getenv("DYNOS_REPLAY_BUNDLE"));
    }
    return 0;
}
