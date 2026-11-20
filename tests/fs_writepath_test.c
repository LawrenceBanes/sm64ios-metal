#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#include "pc/fs/fs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char root[] = "/tmp/sm64-writepath-test-XXXXXX";
    assert(mkdtemp(root) != NULL);
    assert(snprintf(fs_writepath, SYS_MAX_PATH, "%s", root) > 0);

    char directory[512];
    snprintf(directory, sizeof(directory), "%s/hd_textures", root);
    assert(mkdir(directory, 0700) == 0);
    snprintf(directory, sizeof(directory), "%s/hd_textures/gfx", root);
    assert(mkdir(directory, 0700) == 0);
    snprintf(directory, sizeof(directory), "%s/hd_textures/gfx/actors", root);
    assert(mkdir(directory, 0700) == 0);
    snprintf(directory, sizeof(directory), "%s/hd_textures/gfx/actors/goomba", root);
    assert(mkdir(directory, 0700) == 0);
    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/face.png", directory);
    FILE *file = fopen(file_path, "wb");
    assert(file);
    static const char expected[] = "hd texture fixture";
    assert(fwrite(expected, 1, sizeof(expected), file) == sizeof(expected));
    assert(fclose(file) == 0);

    uint64_t size = 0;
    char *data = fs_load_write_file("hd_textures/gfx/actors/goomba/face.png",
                                    sizeof(expected), &size);
    assert(data && size == sizeof(expected));
    assert(memcmp(data, expected, sizeof(expected)) == 0);
    free(data);
    assert(fs_load_write_file("hd_textures/gfx/actors/goomba/face.png",
                              sizeof(expected) - 1, NULL) == NULL);
    assert(fs_load_write_file("../escape.png", 1024, NULL) == NULL);
    assert(fs_load_write_file("hd_textures/../../escape.png", 1024, NULL) == NULL);
    assert(fs_load_write_file("/tmp/escape.png", 1024, NULL) == NULL);
    assert(fs_load_write_file("hd_textures\\gfx\\escape.png", 1024, NULL) == NULL);


    char library[512], scenes[512], resource[512], index[512];
    snprintf(library, sizeof(library), "%s/oot-assets", root);
    snprintf(scenes, sizeof(scenes), "%s/oot-assets/scenes", root);
    snprintf(resource, sizeof(resource), "%s/oot-assets/scenes/4100.wsc", root);
    snprintf(index, sizeof(index), "%s/oot-assets/runtime-index.tsv", root);
    assert(mkdir(library, 0700) == 0 && mkdir(scenes, 0700) == 0);
    file = fopen(resource, "wb"); assert(file); fputs("WSC5", file); fclose(file);
    file = fopen(index, "wb"); assert(file);
    fputs("OOTLIB1\nscenes/4100.wsc\tscenes/4100.wsc\t4\n", file); fclose(file);
    data = fs_load_scene_resource("scenes/4100.wsc", 10, &size);
    assert(data && size == 4 && !memcmp(data, "WSC5", 4)); free(data);
    assert(!fs_load_scene_resource("scenes/4100.wsc", 3, NULL));
    assert(!fs_load_scene_resource("../scenes/4100.wsc", 10, NULL));
    fs_scene_library_reset();
    file = fopen(index, "wb"); assert(file);
    fputs("OOTLIB1\nscenes/4100.wsc\t../outside\t4\n", file); fclose(file);
    assert(!fs_load_scene_resource("scenes/4100.wsc", 10, NULL));
    fs_scene_library_reset();
    file = fopen(index, "wb"); assert(file); fputs("OOTLIB2\n", file); fclose(file);
    assert(!fs_load_scene_resource("scenes/4100.wsc", 10, NULL));
    fs_scene_library_reset();
    assert(unlink(resource) == 0);
    assert(symlink(file_path, resource) == 0);
    file = fopen(index, "wb"); assert(file);
    fprintf(file, "OOTLIB1\nscenes/4100.wsc\tscenes/4100.wsc\t%zu\n", sizeof(expected)); fclose(file);
    assert(!fs_load_scene_resource("scenes/4100.wsc", 1024, NULL));
    fs_scene_library_reset();
    assert(unlink(resource) == 0 && unlink(index) == 0);
    assert(rmdir(scenes) == 0 && rmdir(library) == 0);
    assert(unlink(file_path) == 0);
    assert(rmdir(directory) == 0);
    char parent[512];
    snprintf(parent, sizeof(parent), "%s/hd_textures/gfx/actors", root);
    assert(rmdir(parent) == 0);
    snprintf(parent, sizeof(parent), "%s/hd_textures/gfx", root);
    assert(rmdir(parent) == 0);
    snprintf(parent, sizeof(parent), "%s/hd_textures", root);
    assert(rmdir(parent) == 0);
    assert(rmdir(root) == 0);
    puts("write-path file loading, size limit, and traversal checks passed");
    return 0;
}
