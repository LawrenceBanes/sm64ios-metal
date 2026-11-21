#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "pc/fs/fs.h"

char fs_gamedir[SYS_MAX_PATH];
char fs_writepath[SYS_MAX_PATH];

char *sys_strdup(const char *src) {
    size_t size = strlen(src) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, src, size);
    return copy;
}

bool fs_sys_file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

bool fs_sys_dir_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool fs_sys_mkdir(const char *path) {
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

static bool path_exists(const char *path) {
    return access(path, F_OK) == 0;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char zip_path[SYS_MAX_PATH], mods_dir[SYS_MAX_PATH], dynos_dir[SYS_MAX_PATH];
    char installed[SYS_MAX_PATH], error[512], path[SYS_MAX_PATH];
    bool has_lua, has_dynos;
    snprintf(zip_path, sizeof(zip_path), "%s/lua_actor.zip", argv[1]);
    snprintf(mods_dir, sizeof(mods_dir), "%s/mods", argv[1]);
    snprintf(dynos_dir, sizeof(dynos_dir), "%s/dynos/packs", argv[1]);

    assert(fs_zip_extract_package(zip_path, mods_dir, dynos_dir, installed, sizeof(installed),
                                  &has_lua, &has_dynos, error, sizeof(error)));
    assert(has_lua && has_dynos);
    snprintf(path, sizeof(path), "%s/main.lua", installed);
    assert(path_exists(path));
    snprintf(path, sizeof(path), "%s/actors/Demo/model.bin", installed);
    assert(path_exists(path));
    snprintf(path, sizeof(path), "%s/actors/Demo/model.tex", installed);
    assert(path_exists(path));

    snprintf(zip_path, sizeof(zip_path), "%s/mixed_mod.zip", argv[1]);
    assert(fs_zip_extract_package(zip_path, mods_dir, dynos_dir, installed, sizeof(installed),
                                  &has_lua, &has_dynos, error, sizeof(error)));
    assert(has_lua && has_dynos);
    assert(strstr(error, "ignored unrelated archive entries") != NULL);
    snprintf(path, sizeof(path), "%s/main.lua", installed);
    assert(path_exists(path));
    snprintf(path, sizeof(path), "%s/actions.lua", installed);
    assert(path_exists(path));
    snprintf(path, sizeof(path), "%s/actors/bowser.bin", installed);
    assert(path_exists(path));
    snprintf(path, sizeof(path), "%s/Template Character/example.lua", installed);
    assert(!path_exists(path));
    snprintf(path, sizeof(path), "%s/.vs/workspace.db", installed);
    assert(path_exists(path)); /* Preserve content inside the selected mod root. */

    snprintf(zip_path, sizeof(zip_path), "%s/multiple_mods.zip", argv[1]);
    assert(!fs_zip_extract_package(zip_path, mods_dir, dynos_dir, installed, sizeof(installed),
                                   &has_lua, &has_dynos, error, sizeof(error)));
    assert(strstr(error, "multiple Lua mod roots") != NULL);

    snprintf(zip_path, sizeof(zip_path), "%s/dynos_only.zip", argv[1]);
    bool extracted = fs_zip_extract_package(zip_path, mods_dir, dynos_dir, installed, sizeof(installed),
                                            &has_lua, &has_dynos, error, sizeof(error));
    if (!extracted) fprintf(stderr, "DynOS import failed: %s\n", error);
    assert(extracted);
    assert(!has_lua && has_dynos);
    assert(strncmp(installed, dynos_dir, strlen(dynos_dir)) == 0);
    snprintf(path, sizeof(path), "%s/actors/Demo/model.bin", installed);
    assert(path_exists(path));

    snprintf(zip_path, sizeof(zip_path), "%s/unrecognized_asset.zip", argv[1]);
    assert(!fs_zip_extract_package(zip_path, mods_dir, dynos_dir, installed, sizeof(installed),
                                   &has_lua, &has_dynos, error, sizeof(error)));
    assert(strstr(error, "neither Lua scripts nor recognized DynOS assets") != NULL);

    snprintf(zip_path, sizeof(zip_path), "%s/traversal.zip", argv[1]);
    assert(!fs_zip_extract_package(zip_path, mods_dir, dynos_dir, installed, sizeof(installed),
                                   &has_lua, &has_dynos, error, sizeof(error)));
    snprintf(path, sizeof(path), "%s/escape.lua", argv[1]);
    assert(!path_exists(path));

    snprintf(zip_path, sizeof(zip_path), "%s/case_collision.zip", argv[1]);
    assert(!fs_zip_extract_package(zip_path, mods_dir, dynos_dir, installed, sizeof(installed),
                                   &has_lua, &has_dynos, error, sizeof(error)));

    puts("ZIP import validation, classification, extraction, and path safety passed");
    return 0;
}
