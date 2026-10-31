#include "../src/pc/gfx/gfx_metal_shader.h"

#include <stdio.h>
#include <string.h>

static uint32_t make_shader(const uint8_t rgb[4], const uint8_t alpha[4], uint32_t options) {
    uint32_t id = options;
    for (int i = 0; i < 4; ++i) {
        id |= (uint32_t)(rgb[i] & 7u) << (i * 3);
        id |= (uint32_t)(alpha[i] & 7u) << (12 + i * 3);
    }
    return id;
}

static int emit_shader_id(const char *directory, const char *name, uint32_t shader_id) {
    char path[1024];
    char source[16384];
    struct CCFeatures features;
    int length = snprintf(path, sizeof(path), "%s/%s.metal", directory, name);
    if (length < 0 || (size_t)length >= sizeof(path)) return 0;
    if (!gfx_metal_build_shader(shader_id, source, sizeof(source), &features)) {
        fprintf(stderr, "shader generation failed for %s\n", name);
        return 0;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        return 0;
    }
    size_t size = strlen(source);
    int ok = fwrite(source, 1, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    if (!ok) fprintf(stderr, "could not write %s\n", path);
    else printf("generated %s\n", path);
    return ok;
}

static int emit_present_shader(const char *directory) {
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s.metal", directory, "enhanced_present");
    if (length < 0 || (size_t)length >= sizeof(path)) return 0;
    FILE *file = fopen(path, "wb");
    if (!file) { perror(path); return 0; }
    const char *source = gfx_metal_present_shader_source();
    size_t size = strlen(source);
    int ok = fwrite(source, 1, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    if (!ok) fprintf(stderr, "could not write %s\n", path);
    else printf("generated %s\n", path);
    return ok;
}

static int emit(const char *directory, const char *name, const uint8_t rgb[4],
                const uint8_t alpha[4], uint32_t options) {
    return emit_shader_id(directory, name, make_shader(rgb, alpha, options));
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    const uint8_t flat[4] = {0, 0, 0, SHADER_INPUT_1};
    const uint8_t rgba[4] = {SHADER_TEXEL0, 0, SHADER_INPUT_1, 0};
    const uint8_t dual[4] = {SHADER_TEXEL0, SHADER_TEXEL1, SHADER_INPUT_1, SHADER_TEXEL0};
    const uint8_t alpha_dual[4] = {SHADER_TEXEL0, SHADER_TEXEL1, SHADER_INPUT_1, SHADER_INPUT_2};
    return emit(argv[1], "untextured", flat, flat, 0) &&
           emit(argv[1], "rgba_texture_fog_edge", rgba, rgba,
                SHADER_OPT_ALPHA | SHADER_OPT_FOG | SHADER_OPT_TEXTURE_EDGE) &&
           emit(argv[1], "dual_texture", dual, alpha_dual, SHADER_OPT_ALPHA) &&
           emit(argv[1], "alpha_noise", flat, flat, SHADER_OPT_ALPHA | SHADER_OPT_NOISE) &&
           emit(argv[1], "alpha_texel", rgba, rgba, SHADER_OPT_ALPHA) &&
           emit_shader_id(argv[1], "device_failure_01200200", 0x01200200u) &&
           emit_shader_id(argv[1], "device_failure_00000045", 0x00000045u) &&
           emit_shader_id(argv[1], "device_failure_00000200", 0x00000200u) &&
           emit_shader_id(argv[1], "device_failure_01200a00", 0x01200a00u) &&
           emit_present_shader(argv[1]) ? 0 : 1;
}
