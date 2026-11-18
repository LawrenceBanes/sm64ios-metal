/* Run the production display-list interpreter up to the graphics API boundary.
 * The test mounts the generated base pack and records draws and texture uploads. */
#include <stdarg.h>

#define TARGET_OS_IOS 1  // Exercise iOS diagnostic accounting with host stubs.
#include "src/pc/gfx/gfx_pc.c"
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
static unsigned section_summaries;
static struct CCFeatures features;
static bool expect_alpha_one_shader;
static bool saw_alpha_one_shader;

void gfx_ios_diag_log(const char *format, ...) {
    if (!strncmp(format, "Render section ", sizeof("Render section ") - 1)) {
        ++section_summaries;
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        putchar('\n');
    }
}

bool gfx_ios_texture_cache_diagnostics_enabled(void) { return false; }
int gfx_ios_render_diagnostic_mode(void) { return GFX_IOS_RENDER_TRIANGLES; }

static void record_draw(float *stream, size_t floats, size_t triangles) {
    for (size_t i = 0; i < floats; ++i) assert(isfinite(stream[i]));
#ifdef REPLAY_FIXTURE
    if (!expect_alpha_one_shader) {
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
    if (features.opt_const_alpha_one) {
        saw_alpha_one_shader = true;
    }
    if (expect_alpha_one_shader) {
        assert(features.opt_alpha && features.opt_const_alpha_one);
    }
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

static void replay_passes(bool connected) {
    const Gfx *passes[] = { hyrule_field_opaque_dl, hyrule_field_translucent_dl };
    const Gfx expected_modes[] = {
        gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2),
        gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2)
    };
    for (size_t i = 0; i < 2; ++i) {
        unsigned start_draws = draws;
        unsigned start_summaries = section_summaries;
        rsp.geometry_mode = G_FOG | G_CULL_FRONT | G_TEXTURE_GEN;
        rsp.fog_offset = 255;
        gfx_run_dl((Gfx *)passes[i]);
        gfx_flush();
        assert(section_summaries == start_summaries + 1);
        assert(gfx_render_section.id == 0);
        assert(gfx_render_section.queued == draws - start_draws);
        assert(gfx_render_section.input == gfx_render_section.queued + gfx_render_section.clipped +
                gfx_render_section.cull_front + gfx_render_section.cull_back +
                gfx_render_section.cull_both + gfx_render_section.missing_binding);
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

static void check_rejection_counters(void) {
    static Gfx rejects[] = {
        GFX_RENDER_SECTION_BEGIN(3),
        gsSPSetGeometryMode(G_CULL_BACK),
        gsSP1Triangle(0, 2, 1, 0),
        gsSP1Triangle(3, 4, 5, 0),
        GFX_RENDER_SECTION_END(3),
        gsSPEndDisplayList(),
    };
    rsp.geometry_mode = 0;
    rsp.loaded_vertices[0] = (struct LoadedVertex){.x = 0, .y = 0, .w = 1};
    rsp.loaded_vertices[1] = (struct LoadedVertex){.x = 0.5f, .y = 0, .w = 1};
    rsp.loaded_vertices[2] = (struct LoadedVertex){.x = 0, .y = 0.5f, .w = 1};
    for (int i = 3; i < 6; ++i) rsp.loaded_vertices[i].clip_rej = 2;
    gfx_run_dl(rejects);
    assert(gfx_render_section.input == 2 && gfx_render_section.queued == 0);
    assert(gfx_render_section.cull_back == 1 && gfx_render_section.clipped == 1);
    assert(gfx_render_section.clip_planes == 2);
}

static void check_alpha_constant_detection(void) {
    /* RDP's alpha expression is (A - B) * C + D. D=1 is sufficient when
     * A=B or C=0, and is not sufficient for a genuinely varying expression. */
    assert(gfx_alpha_combiner_is_constant_one(2, 2, 4, G_ACMUX_1));
    assert(gfx_alpha_combiner_is_constant_one(2, 5, G_ACMUX_0, G_ACMUX_1));
    assert(!gfx_alpha_combiner_is_constant_one(2, 5, 4, G_ACMUX_1));
    assert(!gfx_alpha_combiner_is_constant_one(2, 2, 4, G_ACMUX_SHADE));
}

static void check_alpha_constant_one_reaches_metal_shader(void) {
    static Gfx commands[] = {
        gsDPSetCombineLERP(SHADE, 0, 1, 0, SHADE, SHADE, SHADE, 1,
                           SHADE, 0, 1, 0, SHADE, SHADE, SHADE, 1),
        gsSP1Triangle(0, 1, 2, 0),
        gsSPEndDisplayList(),
    };
    rsp.geometry_mode = 0;
    rsp.loaded_vertices[0] = (struct LoadedVertex){.x = -0.5f, .y = -0.5f, .z = 0, .w = 1};
    rsp.loaded_vertices[1] = (struct LoadedVertex){.x =  0.5f, .y = -0.5f, .z = 0, .w = 1};
    rsp.loaded_vertices[2] = (struct LoadedVertex){.x =  0.0f, .y =  0.5f, .z = 0, .w = 1};
    expect_alpha_one_shader = true;
    saw_alpha_one_shader = false;
    gfx_run_dl(commands);
    gfx_flush();
    expect_alpha_one_shader = false;
    assert(saw_alpha_one_shader);
}

int main(int argc, char **argv) {
    assert(argc == 2 && fs_mount(argv[1]));
    check_alpha_constant_detection();
    gfx_rapi = &recorder;
    gfx_current_dimensions.width = 320;
    gfx_current_dimensions.height = 240;
    gfx_sp_reset();
    gfx_render_section_new_frame();
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
    check_alpha_constant_one_reaches_metal_shader();

    assert(draws && uploads && !gfx_texture_cache_failed_binding_count);
    if (getenv("REPLAY_EXPECT_MISSING_HYRULE_TEXTURE"))
        assert(missing_hyrule_uploads > 0);
    else
        assert(missing_hyrule_uploads == 0);
    check_rejection_counters();
    printf("Hyrule host replay: %u triangles submitted, %u texture uploads\n", draws, uploads);
    return 0;
}
