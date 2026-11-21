#include "gfx_metal_shader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 3) return 64;
    bool positions = strcmp(argv[2], "positions") == 0;
    bool material_textures = strcmp(argv[2], "textured-position-lit") == 0;
    bool position_lighting = strcmp(argv[2], "position-lit") == 0 || material_textures;
    bool lighting = strcmp(argv[2], "lighting") == 0;
    bool normals = lighting || strcmp(argv[2], "normals") == 0;
    positions = positions || position_lighting;
    lighting = lighting || position_lighting;
    if (!positions && !position_lighting && !normals) return 64;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return 65;
    long length = ftell(file);
    if (length <= 0 || length > 256 * 1024 || fseek(file, 0, SEEK_SET) != 0) return 66;
    char *material = malloc((size_t)length);
    if (!material || fread(material, 1, (size_t)length, file) != (size_t)length) return 67;
    fclose(file);
    uint32_t shader_id = (normals ? SHADER_OPT_MATERIAL_NORMAL : 0u) |
                         (lighting ? SHADER_OPT_MATERIAL_LIGHT : 0u);
    char generated[32768];
    struct CCFeatures features;
    bool ok = gfx_metal_build_shader_with_material(shader_id, material, (size_t)length,
        material_textures, positions, generated, sizeof(generated), &features);
    free(material);
    if (!ok || !fputs(generated, stdout)) return 68;
    return 0;
}
