#include "gfx_shader64_pack.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) return 64;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return 65;
    long length = ftell(file);
    if (length <= 0 || length > GFX_SHADER64_MAX_SOURCE_BYTES || fseek(file, 0, SEEK_SET) != 0) return 66;
    char *source = malloc((size_t)length);
    char *wrapper = malloc((size_t)length + 2048u);
    if (!source || !wrapper || fread(source, 1, (size_t)length, file) != (size_t)length) return 67;
    fclose(file);
    if (!gfx_shader64_build_shadow_pass_wrapper(source, (size_t)length, wrapper,
                                                (size_t)length + 2048u)) return 68;
    if (fputs(wrapper, stdout) == EOF) return 69;
    free(wrapper);
    free(source);
    return 0;
}
