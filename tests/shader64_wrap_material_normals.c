#include "gfx_shader64_pack.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    static const char prefix[] =
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct MaterialTestIn { float4 position [[position]]; };\n"
        "vertex MaterialTestIn sm64_material_test_vertex(uint id [[vertex_id]]) { MaterialTestIn o; o.position=float4(0.0); return o; }\n";
    static const char suffix[] =
        "\nfragment float4 sm64_material_test_fragment(MaterialTestIn in [[stage_in]], constant float4 &params [[buffer(0)]]) { "
        "return shader64_material(float4(0.5,0.5,0.5,1.0),float2(0.0),in.position,3u,float3(0.0,0.0,1.0),true,params); }\n";
    if (argc != 2) return 64;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return 65;
    long length = ftell(file);
    if (length <= 0 || length > GFX_SHADER64_MAX_SOURCE_BYTES || fseek(file, 0, SEEK_SET) != 0) return 66;
    char *source = malloc((size_t)length);
    if (!source || fread(source, 1, (size_t)length, file) != (size_t)length) return 67;
    fclose(file);
    if (!gfx_shader64_validate_material_normal_source(source, (size_t)length, 0, 0, NULL, 0)) return 68;
    if (fwrite(prefix, 1, sizeof(prefix) - 1, stdout) != sizeof(prefix) - 1 ||
        fwrite(source, 1, (size_t)length, stdout) != (size_t)length ||
        fwrite(suffix, 1, sizeof(suffix) - 1, stdout) != sizeof(suffix) - 1) return 69;
    free(source);
    return 0;
}
