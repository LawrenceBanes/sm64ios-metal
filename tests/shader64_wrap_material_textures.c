#include "gfx_shader64_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    static const char prefix[] =
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct MaterialTextureTestIn { float4 position [[position]]; float2 uv; };\n"
        "vertex MaterialTextureTestIn sm64_material_test_vertex(uint id [[vertex_id]]) { MaterialTextureTestIn o; o.position=float4(0.0); o.uv=float2(0.5); return o; }\n";
    static const char suffix[] =
        "\nfragment float4 sm64_material_test_fragment(MaterialTextureTestIn in [[stage_in]], "
        "texture2d<float> materialTexture0 [[texture(4)]], sampler materialSampler0 [[sampler(4)]], "
        "texture2d<float> materialTexture1 [[texture(5)]], sampler materialSampler1 [[sampler(5)]], "
        "texture2d<float> materialTexture2 [[texture(6)]], sampler materialSampler2 [[sampler(6)]], "
        "texture2d<float> materialTexture3 [[texture(7)]], sampler materialSampler3 [[sampler(7)]], "
        "constant uint &materialTextureCount [[buffer(1)]], constant float4 &params [[buffer(0)]]) { "
        "return shader64_material(float4(0.5),in.uv,in.position,3u,materialTexture0,materialSampler0,"
        "materialTexture1,materialSampler1,materialTexture2,materialSampler2,materialTexture3,"
        "materialSampler3,materialTextureCount,params); }\n";
    if (argc != 2) return 64;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return 65;
    long length = ftell(file);
    if (length <= 0 || length > GFX_SHADER64_MAX_SOURCE_BYTES || fseek(file, 0, SEEK_SET) != 0) return 66;
    char *source = malloc((size_t)length);
    if (!source || fread(source, 1, (size_t)length, file) != (size_t)length) return 67;
    fclose(file);
    if (!gfx_shader64_validate_material_texture_source(source, (size_t)length, NULL, 0)) return 68;
    if (fwrite(prefix, 1, sizeof(prefix) - 1u, stdout) != sizeof(prefix) - 1u ||
        fwrite(source, 1, (size_t)length, stdout) != (size_t)length ||
        fwrite(suffix, 1, sizeof(suffix) - 1u, stdout) != sizeof(suffix) - 1u) return 69;
    free(source);
    return 0;
}
