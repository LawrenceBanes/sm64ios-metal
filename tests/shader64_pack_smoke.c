#include "gfx_shader64_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc != 4) return 64;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return 65;
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) return 66;
    unsigned char *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) return 67;
    fclose(file);
    if (mkdir(argv[2], 0700) != 0) return 68;
    char error[256] = {0};
    int result = gfx_shader64_install_zip(bytes, (size_t)length, argv[2], error, sizeof(error));
    free(bytes);
    if (result != atoi(argv[3])) {
        fprintf(stderr, "expected install=%s, got=%d error=%s\n", argv[3], result, error);
        return 1;
    }
    return 0;
}
