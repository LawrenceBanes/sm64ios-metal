#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include <sys/stat.h>
#include <unistd.h>

#include "sm64.h"
#include "game/area.h"
#include "game/save_file.h"
#include "game/level_update.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/print.h"
#include "game/spawn_object.h"
#include "pc/fs/fs.h"
#include "pc/mods/mod_runtime.h"

char fs_gamedir[SYS_MAX_PATH] = "";
char fs_writepath[SYS_MAX_PATH] = "";
struct MarioState gMarioStates[1];
struct MarioState *gMarioState = &gMarioStates[0];
struct Object *gCurrentObject;
s16 gCurrSaveFileNum = 1;
s16 gCurrLevelNum;
s16 gCurrAreaIndex;

u32 save_file_get_flags(void) { return 0; }
u32 save_file_get_star_flags(s32 file_index, s32 course_index) {
    (void)file_index; (void)course_index; return 0;
}

char *sys_strdup(const char *source) { return source ? strdup(source) : NULL; }

static struct Object sMarioObject;
static struct Object sSpawnedObject;
static struct Controller sController;
static unsigned sPrintedText;

static bool real_mod_storage_contains(const char *root, const char *needle) {
    static const char hex[] = "0123456789abcdef";
    const char *name = "sm64coop-achievements-2.1.0";
    char path[SYS_MAX_PATH];
    int n = snprintf(path, sizeof(path), "%s/mods_storage/", root);
    if (n <= 0 || (size_t)n >= sizeof(path)) return false;
    size_t prefix = (size_t)n;
    for (size_t i = 0; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (prefix + i * 2 + 8 >= sizeof(path)) return false;
        path[prefix + i * 2] = hex[c >> 4];
        path[prefix + i * 2 + 1] = hex[c & 15];
    }
    snprintf(path + prefix + strlen(name) * 2, sizeof(path) - prefix - strlen(name) * 2, ".sav");
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char data[16384];
    size_t length = fread(data, 1, sizeof(data) - 1, file);
    bool ok = !ferror(file) && feof(file);
    fclose(file);
    if (!ok) return false;
    data[length] = '\0';
    bool found = strstr(data, needle) != NULL;
    if (!found) fprintf(stderr, "real mod storage missing '%s' in %s: %s\n", needle, path, data);
    return found;
}

static bool list_add(fs_pathlist_t *list, const char *path) {
    if (list->numpaths == list->listcap) {
        int next_cap = list->listcap ? list->listcap * 2 : 16;
        char **next = realloc(list->paths, sizeof(char *) * (size_t)next_cap);
        if (!next) return false;
        list->paths = next; list->listcap = next_cap;
    }
    list->paths[list->numpaths] = strdup(path);
    if (!list->paths[list->numpaths]) return false;
    list->numpaths++;
    return true;
}

static bool walk_dir(const char *base, walk_fn_t callback, void *user, bool recur) {
    DIR *dir = opendir(base);
    if (!dir) return false;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char full[SYS_MAX_PATH];
        int n = snprintf(full, sizeof(full), "%s/%s", base, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) { closedir(dir); return false; }
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (recur && !walk_dir(full, callback, user, recur)) { closedir(dir); return false; }
        } else if (S_ISREG(st.st_mode) && !callback(user, full)) { closedir(dir); return false; }
    }
    closedir(dir);
    return true;
}

bool fs_sys_walk(const char *base, walk_fn_t walk, void *user, const bool recur) {
    return walk_dir(base, walk, user, recur);
}

static bool enum_cb(void *user, const char *path) { return list_add(user, path); }

fs_pathlist_t fs_sys_enumerate(const char *base, const bool recur) {
    fs_pathlist_t list = {0};
    if (!walk_dir(base, enum_cb, &list, recur)) fs_pathlist_free(&list);
    return list;
}

void fs_pathlist_free(fs_pathlist_t *list) {
    if (!list) return;
    for (int i = 0; i < list->numpaths; ++i) free(list->paths[i]);
    free(list->paths); *list = (fs_pathlist_t){0};
}

bool fs_sys_dir_exists(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISDIR(st.st_mode); }
bool fs_sys_file_exists(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode); }
bool fs_sys_mkdir(const char *path) { return mkdir(path, 0777) == 0 || errno == EEXIST; }
bool fs_sys_copy_file(const char *src, const char *dst) { (void)src; (void)dst; return false; }

const char *fs_get_write_path(const char *path) {
    static char output[SYS_MAX_PATH];
    snprintf(output, sizeof(output), "%s/%s", fs_writepath, path);
    return output;
}

#ifndef MOD_RUNTIME_REAL_ZIP
bool fs_zip_extract_package(const char *zip_path, const char *mods_dir, const char *dynos_dir,
                            char *installed, size_t installed_size, bool *contains_lua,
                            bool *contains_dynos, char *error, size_t error_size) {
    (void)zip_path; (void)mods_dir; (void)dynos_dir; (void)installed; (void)installed_size;
    if (contains_lua) *contains_lua = false;
    if (contains_dynos) *contains_dynos = false;
    if (error && error_size) snprintf(error, error_size, "not part of runtime unit test");
    return false;
}
#endif

u32 set_mario_action(struct MarioState *m, u32 action, u32 arg) {
    m->prevAction = m->action; m->action = action; m->actionArg = arg;
    m->actionState = m->actionTimer = 0;
    return TRUE;
}

s32 perform_ground_step(struct MarioState *m) {
    assert(m == gMarioState);
    return GROUND_STEP_HIT_WALL;
}

s32 perform_air_step(struct MarioState *m, u32 step_arg) {
    assert(m == gMarioState);
    return (s32)(100 + step_arg);
}

u32 perform_water_step(struct MarioState *m) {
    assert(m == gMarioState);
    return WATER_STEP_CANCELLED;
}

s16 set_mario_animation(struct MarioState *m, s32 id) { (void)m; return (s16)id; }

struct Object *spawn_object(struct Object *parent, s32 model, const BehaviorScript *behavior) {
    (void)model;
    memset(&sSpawnedObject, 0, sizeof(sSpawnedObject));
    sSpawnedObject.parentObj = parent; sSpawnedObject.behavior = behavior;
    sSpawnedObject.activeFlags = ACTIVE_FLAG_ACTIVE;
    return &sSpawnedObject;
}

struct Object *spawn_object_abs_with_rot(struct Object *parent, s16 spawn_type, u32 model,
    const BehaviorScript *behavior, s16 x, s16 y, s16 z, s16 pitch, s16 yaw, s16 roll) {
    (void)spawn_type;
    struct Object *obj = spawn_object(parent, (s32)model, behavior);
    obj->oPosX = x; obj->oPosY = y; obj->oPosZ = z;
    obj->oFaceAnglePitch = pitch; obj->oFaceAngleYaw = yaw; obj->oFaceAngleRoll = roll;
    return obj;
}

void print_text(s32 x, s32 y, const char *text) { (void)x; (void)y; (void)text; ++sPrintedText; }

static void reset_game_state(void) {
    memset(gMarioStates, 0, sizeof(gMarioStates));
    memset(&sMarioObject, 0, sizeof(sMarioObject));
    memset(&sSpawnedObject, 0, sizeof(sSpawnedObject));
    memset(&sController, 0, sizeof(sController));
    gMarioState = &gMarioStates[0];
    gMarioState->marioObj = &sMarioObject;
    gMarioState->controller = &sController;
    gMarioState->action = ACT_IDLE;
    sController.buttonPressed = INPUT_A_PRESSED;
}

static int bytecode_writer(lua_State *L, const void *data, size_t size, void *userdata) {
    (void)L;
    return fwrite(data, 1, size, (FILE *)userdata) != size;
}

static void create_bytecode_fixture(const char *fixture_root) {
    char path[SYS_MAX_PATH];
    snprintf(path, sizeof(path), "%s/mods/01_require/scripts/precompiled.luac", fixture_root);
    lua_State *compiler = luaL_newstate();
    assert(compiler);
    assert(luaL_loadstring(compiler, "return 3") == LUA_OK);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(lua_dump(compiler, bytecode_writer, file, 0) == 0);
    assert(fclose(file) == 0);
    lua_close(compiler);
}

static void write_bytecode_script(const char *path, const char *source, unsigned other_pointer_width) {
    lua_State *compiler = luaL_newstate();
    assert(compiler);
    assert(luaL_loadstring(compiler, source) == LUA_OK);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(lua_dump(compiler, bytecode_writer, file, 0) == 0);
    assert(fclose(file) == 0);
    lua_close(compiler);
    if (other_pointer_width) {
        file = fopen(path, "r+b");
        assert(file);
        assert(fseek(file, 13, SEEK_SET) == 0);
        assert(fputc((int)other_pointer_width, file) != EOF);
        assert(fclose(file) == 0);
    }
}

static void create_architecture_variant_fixture(const char *fixture_root) {
    char directory[SYS_MAX_PATH], path[SYS_MAX_PATH];
    snprintf(directory, sizeof(directory), "%s/mods/07_architecture_variants", fixture_root);
    assert(fs_sys_mkdir(directory));
    snprintf(path, sizeof(path), "%s/main.lua", directory);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs("-- paired Lua 5.3 bytecode architecture fixture\n", file) >= 0);
    assert(fclose(file) == 0);

    bool is_64_bit = sizeof(void *) == 8;
    snprintf(path, sizeof(path), "%s/%s", directory, is_64_bit ? "00-64.lua" : "00-32.lua");
    write_bytecode_script(path, "gMarioState.numCoins = gMarioState.numCoins + 64", 0);
    snprintf(path, sizeof(path), "%s/%s", directory, is_64_bit ? "01-32.lua" : "01-64.lua");
    write_bytecode_script(path, "gMarioState.numCoins = gMarioState.numCoins + 1000", is_64_bit ? 4 : 8);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--external-root") == 0) {
        snprintf(fs_writepath, sizeof(fs_writepath), "%s", argv[2]);
        assert(fs_sys_mkdir(fs_writepath));
        reset_game_state();
        assert(mod_runtime_init());
        printf("external mod inventory: %d\n", mod_runtime_mod_count());
        mod_runtime_update();
        for (int i = 0; i < mod_runtime_mod_count(); ++i) {
            printf("external mod %s: %s\n", mod_runtime_mod_name(i),
                   mod_runtime_mod_enabled(i) ? "enabled" : "disabled");
        }
        mod_runtime_shutdown();
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--real-zip") == 0) {
        snprintf(fs_writepath, sizeof(fs_writepath), "%s", argv[2]);
        assert(fs_sys_mkdir(fs_writepath));
        reset_game_state();
        char installed[SYS_MAX_PATH], error[512];
        assert(mod_runtime_import_zip(argv[3], installed, sizeof(installed), error, sizeof(error)));
        printf("real ZIP import: PASS (%s)\n", installed);
        assert(mod_runtime_init());
        assert(mod_runtime_mod_count() == 1);
        printf("real ZIP discovered mod: %s\n", mod_runtime_mod_name(0));
        mod_runtime_update();
        assert(mod_runtime_menu_item_count(0) == 11);
        ModRuntimeMenuItem menu_item;
        assert(mod_runtime_menu_get_item(0, 0, &menu_item));
        assert(menu_item.type == MOD_RUNTIME_MENU_SLIDER);
        assert(menu_item.minimum == 1 && menu_item.maximum == 8);
        assert(mod_runtime_request_menu_set_value(0, 0, 2));
        mod_runtime_update();
        assert(mod_runtime_menu_get_item(0, 0, &menu_item));
        assert(menu_item.value == 2);
        gMarioState->numStars = 1;
        for (int frame = 0; frame < 15; ++frame) mod_runtime_after_mario_update(gMarioState);
        assert(real_mod_storage_contains(argv[2], "73315f676574315374617273=31"));
        /* Unsupported sound/HUD calls should be diagnosed and isolated to their hooks. */
        mod_runtime_update();
        mod_runtime_hud_render();
        assert(mod_runtime_mod_enabled(0));
        printf("real ZIP startup: %s\n", mod_runtime_mod_enabled(0) ? "loaded" : "failed/disabled");
        assert(mod_runtime_set_enabled(0, false));
        assert(mod_runtime_menu_item_count(0) == 0);
        assert(mod_runtime_set_enabled(0, true));
        assert(mod_runtime_menu_item_count(0) == 11);
        mod_runtime_shutdown();
        assert(mod_runtime_init());
        mod_runtime_update();
        gMarioState->numStars = 1;
        mod_runtime_after_mario_update(gMarioState);
        assert(real_mod_storage_contains(argv[2], "73315f676574315374617273=31"));
        mod_runtime_shutdown();
        return 0;
    }
    assert(argc == 2);
    snprintf(fs_writepath, sizeof(fs_writepath), "%s", argv[1]);
    create_bytecode_fixture(argv[1]);
    create_architecture_variant_fixture(argv[1]);
    reset_game_state();
    assert(mod_runtime_init());
    assert(mod_runtime_mod_count() == 8);
    assert(mod_runtime_dynos_pack_count() == 1);
    assert(strcmp(mod_runtime_dynos_pack_name(0), "FixturePack") == 0);
    assert(strstr(mod_runtime_dynos_pack_summary(0), "1 model bins · 2 texture assets") != NULL);
    assert(strstr(mod_runtime_dynos_pack_summary(0), "inactive") != NULL);
    assert(strcmp(mod_runtime_mod_name(0), "00_core") == 0);
    assert(mod_runtime_mod_enabled(0));

    mod_runtime_update();
    assert(mod_runtime_mod_enabled(7));
    assert(gMarioState->numCoins == 178); /* Native-width bytecode runs; the alternate-width twin is skipped. */
    assert(!mod_runtime_mod_enabled(4));
    assert(mod_runtime_mod_enabled(2)); /* Missing API callback failure is isolated to that hook. */
    assert(gMarioState->numCoins == 178); /* Relative modules, per-mod cache isolation, architecture skip, and coroutine budget. */
    assert(gMarioState->numLives == 111); /* Module and callback-relative require plus isolated failure. */
    assert(!mod_runtime_set_enabled(4, true)); /* Failed initialization was cleaned and remains disabled. */
    assert((gMarioState->action & ACT_ID_MASK) >= 0x1c0u);
    assert((gMarioState->action & ACT_ID_MASK) <= 0x1ffu);

    mod_runtime_update();
    assert(gMarioState->numCoins == 192);
    assert(gMarioState->numLives == 111);

    mod_runtime_after_mario_update(gMarioState);
    assert(gMarioState->pos[0] == 1.0f);
    assert(gMarioState->pos[2] == 5.0f);

    int in_loop = 0;
    assert(mod_runtime_mario_action(gMarioState, &in_loop));
    assert(!in_loop && gMarioState->actionTimer == 1);
    gMarioState->vel[1] = 10.0f;
    assert(mod_runtime_mario_gravity(gMarioState));
    assert(gMarioState->vel[1] == 8.0f);

    uint32_t incoming = 0x12345678;
    bool cancel = false;
    mod_runtime_before_set_action(gMarioState, &incoming, 7, &cancel);
    assert(!cancel && incoming == ACT_JUMP);

    gCurrentObject = &sSpawnedObject;
    mod_runtime_behavior_tick();
    assert(sSpawnedObject.oPosX == 4.0f && sSpawnedObject.oPosY == 2.0f);
    mod_runtime_behavior_tick();
    assert(sSpawnedObject.oPosY == 4.0f);
    mod_runtime_update();
    assert(gMarioState->numStars == 4);
    assert(gMarioState->numCoins == 206);

    assert(mod_runtime_set_enabled(0, false));
    assert(!mod_runtime_mod_enabled(0));
    int coins_before = gMarioState->numCoins;
    mod_runtime_update();
    assert(gMarioState->numCoins == coins_before + 13);

    assert(mod_runtime_set_enabled(3, false));
    float old_y = sSpawnedObject.oPosY;
    assert(sSpawnedObject.activeFlags == ACTIVE_FLAG_DEACTIVATED);
    mod_runtime_behavior_tick();
    assert(sSpawnedObject.oPosY == old_y);

    assert(mod_runtime_set_enabled(0, true));
    assert(mod_runtime_mod_enabled(0));
    int32_t step_result = -1;
    int coins_before_step_hook = gMarioState->numCoins;
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_AIR, 55, &step_result));
    assert(step_result == AIR_STEP_LANDED);
    assert(gMarioState->numCoins == coins_before_step_hook + 1); /* First output stops later physics hooks. */
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_GROUND, 0, &step_result));
    assert(step_result == GROUND_STEP_NONE);
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_WATER, 0, &step_result));
    assert(step_result == WATER_STEP_CANCELLED);
    assert(mod_runtime_before_phys_step(gMarioState, MOD_RUNTIME_STEP_TYPE_HANG, 0, &step_result));
    assert(step_result == 77);
    assert(!mod_runtime_before_phys_step(gMarioState, 99, 0, &step_result));
    assert(gMarioState->numCoins == coins_before_step_hook + 1);
    gMarioState->health = 4660;
    mod_runtime_update();
    assert(gMarioState->health == GROUND_STEP_HIT_WALL);
    gMarioState->health = 4661;
    mod_runtime_update();
    assert(gMarioState->health == 123);
    gMarioState->health = 4662;
    mod_runtime_update();
    assert(gMarioState->health == WATER_STEP_CANCELLED);
    mod_runtime_shutdown();
    puts("mod runtime host regression checks passed");
    return 0;
}
