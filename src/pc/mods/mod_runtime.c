#include "mod_runtime.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "macros.h"
#include "sm64.h"
#include "types.h"
#include "object_fields.h"
#include "engine/behavior_script.h"
#include "behavior_data.h"
#include "game/level_update.h"
#include "game/mario.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/print.h"
#include "game/spawn_object.h"
#include "pc/fs/fs.h"

#define MOD_MAX_COUNT 128
#define MOD_MAX_NAME 128
#define MOD_MAX_FILES 512
#define MOD_MEMORY_LIMIT (32u * 1024u * 1024u)
#define MOD_INSTRUCTION_LIMIT 50000
#define MOD_INSTRUCTION_QUANTUM 1000
#define MOD_EVENT_COUNT 69
#define MOD_DYNAMIC_ID_BASE 0x70000000u
#define MOD_ACTION_ID_BASE 0x000001c0u
#define MOD_ACTION_ID_CAPACITY 64u

enum { HOOK_UPDATE = 0, HOOK_MARIO_UPDATE = 1, HOOK_BEFORE_MARIO_UPDATE = 2,
       HOOK_ON_SET_MARIO_ACTION = 3, HOOK_BEFORE_PHYS_STEP = 4,
       HOOK_ON_HUD_RENDER = 9, HOOK_ALLOW_INTERACT = 10, HOOK_ON_INTERACT = 11,
       HOOK_ON_LEVEL_INIT = 12, HOOK_ON_EXIT = 34 };

typedef struct Mod Mod;
typedef struct ModHook ModHook;
typedef struct ModAction ModAction;
typedef struct ModBehavior ModBehavior;
typedef struct ModObject ModObject;
typedef struct ModModule ModModule;
typedef struct ModRequest ModRequest;
typedef struct DynosPack DynosPack;
typedef struct ModCustomActionId ModCustomActionId;

struct ModHook { Mod *owner; char *file; int event; int function_ref; bool enabled; ModHook *next_global; ModHook *next_owned; };
struct ModAction { Mod *owner; char *file; uint32_t id; int every_frame_ref; int gravity_ref; bool enabled; ModAction *next_global; ModAction *next_owned; };
struct ModBehavior { Mod *owner; char *file; uint32_t id; int object_list; int init_ref; int loop_ref; bool enabled; ModBehavior *next_global; ModBehavior *next_owned; };
struct ModObject { struct Object *object; uint32_t token; ModBehavior *behavior; bool initialized; bool enabled; ModObject *next; };
struct ModModule { char *name; int value_ref; bool loading; ModModule *next; };
struct ModRequest { int type; int index; bool enabled; char target_name[MOD_MAX_NAME]; char *path; ModRequest *next; };
struct DynosPack {
    char name[MOD_MAX_NAME];
    char path[SYS_MAX_PATH];
    unsigned model_bins, texture_assets, collision_assets, level_assets, behavior_assets, audio_assets, other_assets;
    DynosPack *next;
};
struct ModCustomActionId { Mod *owner; uint32_t id; unsigned slot; ModCustomActionId *next_owned; };

struct Mod {
    char name[MOD_MAX_NAME];
    char path[SYS_MAX_PATH];
    bool enabled;
    bool loaded;
    bool load_failed;
    int env_ref;
    char **files;
    size_t file_count;
    ModModule *modules;
    ModHook *hooks;
    ModAction *actions;
    ModBehavior *behaviors;
    ModCustomActionId *custom_action_ids;
    bool contains_dynos_assets;
    Mod *next;
};

typedef struct { size_t bytes; } LuaAllocator;
typedef enum { LUA_UD_MARIO, LUA_UD_CONTROLLER, LUA_UD_VEC_F, LUA_UD_VEC_S, LUA_UD_OBJECT } LuaUserdataKind;
typedef struct { LuaUserdataKind kind; int field; struct Object *object; uint32_t token; } LuaUserdata;

enum { VEC_MARIO_POS, VEC_MARIO_VEL, VEC_MARIO_FACE_ANGLE, VEC_MARIO_ANGLE_VEL };
enum { HOOK_FN_INIT, HOOK_FN_LOOP, HOOK_FN_ACTION, HOOK_FN_GRAVITY };

static lua_State *sLua;
static LuaAllocator sAllocator;
static Mod *sMods;
static DynosPack *sDynosPacks;
static Mod *sCurrentMod;
static const char *sCurrentFile;
static ModHook *sHooks;
static ModAction *sActions;
static ModBehavior *sBehaviors;
static ModObject *sObjects;
static uint32_t sNextBehaviorId = 1;
static uint32_t sNextObjectToken = 1;
static unsigned sHookFailures;
static unsigned sCallbackDepth;
static int sInstructionBudget;
static bool sInitialized;
static bool sModsLoaded;
static bool sActionSlots[MOD_ACTION_ID_CAPACITY];
static pthread_mutex_t sRequestMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sModListMutex = PTHREAD_MUTEX_INITIALIZER;
static ModRequest *sRequestHead;
static ModRequest *sRequestTail;

enum { MOD_REQUEST_ENABLE = 1, MOD_REQUEST_RELOAD, MOD_REQUEST_IMPORT_ZIP };

static void mod_log(const char *level, const char *mod, const char *message) {
    fprintf(stderr, "[mods][%s][%s] %s\n", level, mod ? mod : "runtime", message ? message : "");
}

static void mod_logf(const char *level, const char *mod, const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    mod_log(level, mod, text);
}

static void *lua_mod_alloc(void *ud, void *ptr, size_t old_size, size_t new_size) {
    LuaAllocator *allocator = (LuaAllocator *)ud;
    (void)old_size;
    if (new_size == 0) {
        if (ptr) {
            allocator->bytes -= (size_t)old_size;
            free(ptr);
        }
        return NULL;
    }
    if (!ptr && new_size > MOD_MEMORY_LIMIT - allocator->bytes) return NULL;
    if (ptr && new_size > old_size && new_size - old_size > MOD_MEMORY_LIMIT - allocator->bytes) return NULL;
    void *next = realloc(ptr, new_size);
    if (next) {
        if (new_size >= old_size) allocator->bytes += new_size - old_size;
        else allocator->bytes -= old_size - new_size;
    }
    return next;
}

static void lua_instruction_budget(lua_State *L, lua_Debug *ar) {
    (void)ar;
    /* Lua 5.3 copies the active hook to threads created by lua_newthread.
     * Keep the hook installed on the main state so coroutines are bounded too.
     * Inactive coroutine threads only pay the hook's small guard cost. */
    if (!sCallbackDepth) return;
    sInstructionBudget -= MOD_INSTRUCTION_QUANTUM;
    if (sInstructionBudget <= 0)
        luaL_error(L, "mod callback exceeded %d Lua instructions", MOD_INSTRUCTION_LIMIT);
}

static void begin_lua_callback(lua_State *L) {
    if (sCallbackDepth++ == 0) {
        sInstructionBudget = MOD_INSTRUCTION_LIMIT;
        lua_sethook(L, lua_instruction_budget, LUA_MASKCOUNT, MOD_INSTRUCTION_QUANTUM);
    }
}

static void end_lua_callback(void) {
    if (sCallbackDepth) --sCallbackDepth;
}

static bool safe_relative_path(const char *path) {
    if (!path || !*path || path[0] == '/' || path[0] == '\\') return false;
    const char *part = path;
    for (const unsigned char *p = (const unsigned char *)path;; ++p) {
        unsigned char c = *p;
        if (c == ':' || c == '\\' || (c != 0 && c < 0x20)) return false;
        if (c == '/' || c == 0) {
            size_t n = (const char *)p - part;
            if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return false;
            if (!c) break;
            part = (const char *)p + 1;
        }
    }
    return true;
}

static bool file_has_suffix(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    if (a < b) return false;
    return strcasecmp(path + a - b, suffix) == 0;
}

static bool file_is_dynos_asset(const char *path) {
    return file_has_suffix(path, ".tex") || file_has_suffix(path, ".png") ||
           file_has_suffix(path, ".bin");
}

static const char *relative_path(const Mod *mod, const char *fullpath) {
    size_t n = strlen(mod->path);
    if (strncmp(mod->path, fullpath, n) || fullpath[n] != '/') return NULL;
    return fullpath + n + 1;
}

static int compare_strings(const void *a, const void *b) {
    const char *const *sa = a, *const *sb = b;
    return strcmp(*sa, *sb);
}

static bool mod_file_walk(void *userdata, const char *path) {
    Mod *mod = (Mod *)userdata;
    if (file_is_dynos_asset(path)) mod->contains_dynos_assets = true;
    if (mod->file_count >= MOD_MAX_FILES) return false;
    if (!file_has_suffix(path, ".lua") && !file_has_suffix(path, ".luac")) return true;
    char *copy = strdup(path);
    if (!copy) return false;
    char **next = realloc(mod->files, sizeof(char *) * (mod->file_count + 1));
    if (!next) { free(copy); return false; }
    mod->files = next;
    mod->files[mod->file_count++] = copy;
    return true;
}

static void add_dynos_pack(const char *name, const char *path) {
    if (!name || !*name || strlen(name) >= MOD_MAX_NAME) return;
    fs_pathlist_t files = fs_sys_enumerate(path, true);
    unsigned model_bins = 0, texture_assets = 0, collision_assets = 0;
    unsigned level_assets = 0, behavior_assets = 0, audio_assets = 0, other_assets = 0;
    for (int i = 0; i < files.numpaths; ++i) {
        const char *file = files.paths[i];
        if (file_has_suffix(file, ".bin")) ++model_bins;
        else if (file_has_suffix(file, ".tex") || file_has_suffix(file, ".png")) ++texture_assets;
        else if (file_has_suffix(file, ".col")) ++collision_assets;
        else if (file_has_suffix(file, ".lvl")) ++level_assets;
        else if (file_has_suffix(file, ".bhv")) ++behavior_assets;
        else if (file_has_suffix(file, ".m64") || file_has_suffix(file, ".mp3")
                 || file_has_suffix(file, ".aiff") || file_has_suffix(file, ".ogg")) ++audio_assets;
        else ++other_assets;
    }
    fs_pathlist_free(&files);

    pthread_mutex_lock(&sModListMutex);
    DynosPack **link = &sDynosPacks;
    while (*link && strcmp((*link)->name, name) < 0) link = &(*link)->next;
    if (*link && !strcmp((*link)->name, name)) { pthread_mutex_unlock(&sModListMutex); return; }
    DynosPack *pack = calloc(1, sizeof(*pack));
    if (!pack) { pthread_mutex_unlock(&sModListMutex); return; }
    snprintf(pack->name, sizeof(pack->name), "%s", name);
    snprintf(pack->path, sizeof(pack->path), "%s", path);
    pack->model_bins = model_bins; pack->texture_assets = texture_assets;
    pack->collision_assets = collision_assets; pack->level_assets = level_assets;
    pack->behavior_assets = behavior_assets; pack->audio_assets = audio_assets;
    pack->other_assets = other_assets;
    pack->next = *link;
    *link = pack;
    pthread_mutex_unlock(&sModListMutex);
}

static bool mod_add_file(Mod *mod, const char *path) {
    if (!mod || mod->file_count >= MOD_MAX_FILES) return false;
    char *copy = strdup(path);
    if (!copy) return false;
    char **next = realloc(mod->files, sizeof(char *) * (mod->file_count + 1));
    if (!next) { free(copy); return false; }
    mod->files = next;
    mod->files[mod->file_count++] = copy;
    return true;
}

static void free_string_list(char **items, size_t count) {
    for (size_t i = 0; i < count; ++i) free(items[i]);
    free(items);
}

static Mod *find_mod(const char *name) {
    for (Mod *mod = sMods; mod; mod = mod->next) if (!strcmp(mod->name, name)) return mod;
    return NULL;
}

static void add_mod(const char *name, const char *path) {
    pthread_mutex_lock(&sModListMutex);
    if (find_mod(name)) { pthread_mutex_unlock(&sModListMutex); return; }
    if (sMods == NULL || strcmp(name, sMods->name) < 0) {
        Mod *mod = calloc(1, sizeof(*mod));
        if (!mod) { pthread_mutex_unlock(&sModListMutex); return; }
        snprintf(mod->name, sizeof(mod->name), "%s", name);
        snprintf(mod->path, sizeof(mod->path), "%s", path);
        mod->env_ref = LUA_NOREF;
        mod->enabled = true;
        mod->next = sMods;
        sMods = mod;
        pthread_mutex_unlock(&sModListMutex);
        return;
    }
    Mod *at = sMods;
    while (at->next && strcmp(at->next->name, name) < 0) at = at->next;
    if (!strcmp(at->name, name) || (at->next && !strcmp(at->next->name, name))) { pthread_mutex_unlock(&sModListMutex); return; }
    Mod *mod = calloc(1, sizeof(*mod));
    if (!mod) { pthread_mutex_unlock(&sModListMutex); return; }
    snprintf(mod->name, sizeof(mod->name), "%s", name);
    snprintf(mod->path, sizeof(mod->path), "%s", path);
    mod->env_ref = LUA_NOREF;
    mod->enabled = true;
    mod->next = at->next;
    at->next = mod;
    pthread_mutex_unlock(&sModListMutex);
}

static bool discover_mods(void) {
    char root[SYS_MAX_PATH];
    snprintf(root, sizeof(root), "%s/mods", fs_writepath);
    if (!fs_sys_dir_exists(root) && !fs_sys_mkdir(root)) {
        mod_log("error", NULL, "could not create sandbox Mods directory");
        return false;
    }
    fs_pathlist_t paths = fs_sys_enumerate(root, true);
    if (!paths.paths) return false;
    qsort(paths.paths, (size_t)paths.numpaths, sizeof(char *), compare_strings);
    for (int i = 0; i < paths.numpaths; ++i) {
        const char *file = paths.paths[i];
        const char *rel = file + strlen(root);
        while (*rel == '/') ++rel;
        const char *slash = strchr(rel, '/');
        if (slash) {
            size_t n = (size_t)(slash - rel);
            if (!n || n >= MOD_MAX_NAME) continue;
            char name[MOD_MAX_NAME];
            memcpy(name, rel, n); name[n] = 0;
            char dir[SYS_MAX_PATH];
            snprintf(dir, sizeof(dir), "%s/%s", root, name);
            add_mod(name, dir);
        } else if (file_has_suffix(file, ".lua")) {
            char name[MOD_MAX_NAME];
            snprintf(name, sizeof(name), "%s", rel);
            char *dot = strrchr(name, '.'); if (dot) *dot = 0;
            add_mod(name, root);
            mod_add_file(find_mod(name), file);
        }
    }
    fs_pathlist_free(&paths);
    for (Mod *mod = sMods; mod; mod = mod->next) {
        if (!strcmp(mod->path, root)) {
            qsort(mod->files, mod->file_count, sizeof(char *), compare_strings);
            continue;
        }
        fs_sys_walk(mod->path, mod_file_walk, mod, true);
        qsort(mod->files, mod->file_count, sizeof(char *), compare_strings);
    }

    char dynos_root[SYS_MAX_PATH];
    snprintf(dynos_root, sizeof(dynos_root), "%s/dynos/packs", fs_writepath);
    if (fs_sys_dir_exists(dynos_root)) {
        fs_pathlist_t packs = fs_sys_enumerate(dynos_root, true);
        if (packs.paths) {
            for (int i = 0; i < packs.numpaths; ++i) {
                const char *file = packs.paths[i];
                if (!file_is_dynos_asset(file)) continue;
                const char *rel = file + strlen(dynos_root);
                while (*rel == '/') ++rel;
                const char *slash = strchr(rel, '/');
                if (!slash || slash == rel) continue;
                size_t n = (size_t)(slash - rel);
                if (n >= MOD_MAX_NAME) continue;
                char name[MOD_MAX_NAME];
                memcpy(name, rel, n); name[n] = 0;
                char path[SYS_MAX_PATH];
                snprintf(path, sizeof(path), "%s/%s", dynos_root, name);
                add_dynos_pack(name, path);
            }
            fs_pathlist_free(&packs);
        }
    }
    return true;
}

static int callback_ref_from_table(lua_State *L, int table, const char *field) {
    lua_getfield(L, table, field);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return LUA_NOREF; }
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static bool add_hook(Mod *owner, int event, int function_ref) {
    ModHook *hook = calloc(1, sizeof(*hook));
    if (!hook) { luaL_unref(sLua, LUA_REGISTRYINDEX, function_ref); return false; }
    hook->file = strdup(sCurrentFile ? sCurrentFile : "main.lua");
    if (!hook->file) { free(hook); luaL_unref(sLua, LUA_REGISTRYINDEX, function_ref); return false; }
    hook->owner = owner; hook->event = event; hook->function_ref = function_ref; hook->enabled = true;
    hook->next_global = sHooks; sHooks = hook;
    hook->next_owned = owner->hooks; owner->hooks = hook;
    return true;
}

static bool add_action(Mod *owner, uint32_t id, int every_ref, int gravity_ref) {
    ModAction *action = calloc(1, sizeof(*action));
    if (!action) {
        if (every_ref != LUA_NOREF) luaL_unref(sLua, LUA_REGISTRYINDEX, every_ref);
        if (gravity_ref != LUA_NOREF) luaL_unref(sLua, LUA_REGISTRYINDEX, gravity_ref);
        return false;
    }
    action->file = strdup(sCurrentFile ? sCurrentFile : "main.lua");
    if (!action->file) {
        free(action);
        if (every_ref != LUA_NOREF) luaL_unref(sLua, LUA_REGISTRYINDEX, every_ref);
        if (gravity_ref != LUA_NOREF) luaL_unref(sLua, LUA_REGISTRYINDEX, gravity_ref);
        return false;
    }
    action->owner = owner; action->id = id; action->every_frame_ref = every_ref;
    action->gravity_ref = gravity_ref; action->enabled = true;
    action->next_global = sActions; sActions = action;
    action->next_owned = owner->actions; owner->actions = action;
    return true;
}

/* Common callback entry that preserves a requested number of return values. */
static int call_ref(Mod *owner, const char *file, int function_ref, const char *event, int nargs, int nresults) {
    lua_State *L = sLua;
    int base = lua_gettop(L) - nargs;
    lua_rawgeti(L, LUA_REGISTRYINDEX, function_ref);
    lua_insert(L, base + 1);
    Mod *previous_mod = sCurrentMod;
    const char *previous_file = sCurrentFile;
    sCurrentMod = owner;
    sCurrentFile = file;
    begin_lua_callback(L);
    int status = lua_pcall(L, nargs, nresults, 0);
    end_lua_callback();
    sCurrentMod = previous_mod;
    sCurrentFile = previous_file;
    if (status != LUA_OK) {
        const char *message = lua_tostring(L, -1);
        fprintf(stderr, "[mods][error][%s] %s callback: %s\n", owner->name, event,
                message ? message : "unknown Lua error");
        lua_pop(L, 1); ++sHookFailures;
        return 0;
    }
    return 1;
}

static void push_mario(lua_State *L) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_MARIO };
    luaL_getmetatable(L, "sm64.mod.MarioState"); lua_setmetatable(L, -2);
}

static ModObject *find_object(struct Object *object) {
    for (ModObject *entry = sObjects; entry; entry = entry->next) if (entry->object == object && entry->enabled) return entry;
    return NULL;
}

static void push_object(lua_State *L, struct Object *object) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    ModObject *entry = find_object(object);
    *ud = (LuaUserdata){ .kind = LUA_UD_OBJECT, .object = object, .token = entry ? entry->token : 0 };
    luaL_getmetatable(L, "sm64.mod.Object"); lua_setmetatable(L, -2);
}

static void push_vector(lua_State *L, LuaUserdataKind kind, int field) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = kind, .field = field };
    luaL_getmetatable(L, kind == LUA_UD_VEC_F ? "sm64.mod.Vec3f" : "sm64.mod.Vec3s");
    lua_setmetatable(L, -2);
}

static struct MarioState *current_mario(void) { return gMarioState; }

static float *vec_f_pointer(LuaUserdata *ud) {
    struct MarioState *m = current_mario();
    if (!m) return NULL;
    switch (ud->field) {
        case VEC_MARIO_POS: return m->pos;
        case VEC_MARIO_VEL: return m->vel;
        default: return NULL;
    }
}

static int16_t *vec_s_pointer(LuaUserdata *ud) {
    struct MarioState *m = current_mario();
    if (!m) return NULL;
    switch (ud->field) {
        case VEC_MARIO_FACE_ANGLE: return m->faceAngle;
        case VEC_MARIO_ANGLE_VEL: return m->angleVel;
        default: return NULL;
    }
}

static int vector_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, luaL_testudata(L, 1, "sm64.mod.Vec3f") ? "sm64.mod.Vec3f" : "sm64.mod.Vec3s");
    int axis = 0;
    if (lua_isinteger(L, 2)) axis = (int)lua_tointeger(L, 2) - 1;
    else {
        const char *key = luaL_checkstring(L, 2);
        axis = key[0] == 'x' ? 0 : key[0] == 'y' ? 1 : key[0] == 'z' ? 2 : -1;
    }
    if (axis < 0 || axis > 2) return luaL_error(L, "vector index must be x/y/z or 1..3");
    if (ud->kind == LUA_UD_VEC_F) { float *v = vec_f_pointer(ud); lua_pushnumber(L, v ? v[axis] : 0); }
    else { int16_t *v = vec_s_pointer(ud); lua_pushinteger(L, v ? v[axis] : 0); }
    return 1;
}

static int vector_newindex(lua_State *L) {
    LuaUserdata *ud = lua_touserdata(L, 1);
    int axis = 0;
    if (lua_isinteger(L, 2)) axis = (int)lua_tointeger(L, 2) - 1;
    else {
        const char *key = luaL_checkstring(L, 2);
        axis = key[0] == 'x' ? 0 : key[0] == 'y' ? 1 : key[0] == 'z' ? 2 : -1;
    }
    if (axis < 0 || axis > 2) return luaL_error(L, "vector index must be x/y/z or 1..3");
    if (ud->kind == LUA_UD_VEC_F) { float *v = vec_f_pointer(ud); if (v) v[axis] = (float)luaL_checknumber(L, 3); }
    else { int16_t *v = vec_s_pointer(ud); if (v) v[axis] = (int16_t)luaL_checkinteger(L, 3); }
    return 0;
}

static bool mario_number_get(struct MarioState *m, const char *key, lua_State *L) {
#define GET_U32(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, (lua_Integer)m->field); return true; }
#define GET_S32(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, (lua_Integer)m->field); return true; }
#define GET_NUM(name, field) if (!strcmp(key, name)) { lua_pushnumber(L, m->field); return true; }
    GET_U32("action", action) GET_U32("prevAction", prevAction) GET_U32("actionArg", actionArg)
    GET_U32("flags", flags) GET_U32("particleFlags", particleFlags) GET_U32("input", input)
    GET_U32("actionState", actionState) GET_U32("actionTimer", actionTimer)
    GET_U32("wallKickTimer", wallKickTimer) GET_S32("numCoins", numCoins)
    GET_S32("numStars", numStars) GET_S32("numLives", numLives) GET_S32("health", health)
    GET_NUM("forwardVel", forwardVel) GET_NUM("slideVelX", slideVelX) GET_NUM("slideVelZ", slideVelZ)
    GET_NUM("intendedMag", intendedMag) GET_S32("intendedYaw", intendedYaw)
    GET_S32("floorAngle", floorAngle) GET_S32("floorHeight", floorHeight)
    GET_S32("waterLevel", waterLevel) GET_S32("hurtCounter", hurtCounter)
    GET_S32("terrainSoundAddend", terrainSoundAddend)
#undef GET_U32
#undef GET_S32
#undef GET_NUM
    return false;
}

static bool mario_number_set(struct MarioState *m, const char *key, lua_State *L, int value_index) {
#define SET_INT(name, field, type) if (!strcmp(key, name)) { m->field = (type)luaL_checkinteger(L, value_index); return true; }
#define SET_NUM(name, field) if (!strcmp(key, name)) { m->field = (float)luaL_checknumber(L, value_index); return true; }
    SET_INT("action", action, uint32_t) SET_INT("prevAction", prevAction, uint32_t)
    SET_INT("actionArg", actionArg, uint32_t) SET_INT("flags", flags, uint32_t)
    SET_INT("particleFlags", particleFlags, uint32_t) SET_INT("input", input, uint16_t)
    SET_INT("actionState", actionState, uint16_t) SET_INT("actionTimer", actionTimer, uint16_t)
    SET_INT("wallKickTimer", wallKickTimer, uint8_t) SET_INT("health", health, int16_t)
    SET_INT("numCoins", numCoins, int16_t) SET_INT("numStars", numStars, int16_t)
    SET_INT("numLives", numLives, int8_t) SET_INT("intendedYaw", intendedYaw, int16_t)
    SET_INT("floorAngle", floorAngle, int16_t) SET_INT("hurtCounter", hurtCounter, uint8_t)
    SET_NUM("forwardVel", forwardVel) SET_NUM("slideVelX", slideVelX) SET_NUM("slideVelZ", slideVelZ)
    SET_NUM("intendedMag", intendedMag)
#undef SET_INT
#undef SET_NUM
    return false;
}

static int mario_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.MarioState");
    (void)ud;
    struct MarioState *m = current_mario();
    const char *key = luaL_checkstring(L, 2);
    if (!m) { lua_pushnil(L); return 1; }
    if (!strcmp(key, "pos")) push_vector(L, LUA_UD_VEC_F, VEC_MARIO_POS);
    else if (!strcmp(key, "vel")) push_vector(L, LUA_UD_VEC_F, VEC_MARIO_VEL);
    else if (!strcmp(key, "faceAngle")) push_vector(L, LUA_UD_VEC_S, VEC_MARIO_FACE_ANGLE);
    else if (!strcmp(key, "angleVel")) push_vector(L, LUA_UD_VEC_S, VEC_MARIO_ANGLE_VEL);
    else if (!strcmp(key, "controller") && m->controller) {
        LuaUserdata *c = lua_newuserdata(L, sizeof(*c)); *c = (LuaUserdata){ .kind = LUA_UD_CONTROLLER };
        luaL_getmetatable(L, "sm64.mod.Controller"); lua_setmetatable(L, -2);
    } else if (!strcmp(key, "marioObj") && m->marioObj) push_object(L, m->marioObj);
    else if (!strcmp(key, "heldObj") && m->heldObj) push_object(L, m->heldObj);
    else if (!strcmp(key, "interactObj") && m->interactObj) push_object(L, m->interactObj);
    else if (!mario_number_get(m, key, L)) return luaL_error(L, "MarioState field '%s' is unsupported by this adapter", key);
    return 1;
}

static int mario_newindex(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.MarioState");
    (void)ud;
    struct MarioState *m = current_mario();
    const char *key = luaL_checkstring(L, 2);
    if (!m || !mario_number_set(m, key, L, 3)) return luaL_error(L, "MarioState field '%s' is read-only or unsupported", key);
    return 0;
}

static int controller_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.Controller"); (void)ud;
    struct MarioState *m = current_mario();
    const char *key = luaL_checkstring(L, 2);
    if (!m || !m->controller) { lua_pushnil(L); return 1; }
    if (!strcmp(key, "buttonDown")) lua_pushinteger(L, m->controller->buttonDown);
    else if (!strcmp(key, "buttonPressed")) lua_pushinteger(L, m->controller->buttonPressed);
    else if (!strcmp(key, "rawStickX")) lua_pushinteger(L, m->controller->rawStickX);
    else if (!strcmp(key, "rawStickY")) lua_pushinteger(L, m->controller->rawStickY);
    else if (!strcmp(key, "stickX")) lua_pushnumber(L, m->controller->stickX);
    else if (!strcmp(key, "stickY")) lua_pushnumber(L, m->controller->stickY);
    else if (!strcmp(key, "stickMag")) lua_pushnumber(L, m->controller->stickMag);
    else return luaL_error(L, "Controller field '%s' is unsupported by this adapter", key);
    return 1;
}

static int controller_newindex(lua_State *L) { return luaL_error(L, "Controller state is read-only"); }

static struct Object *checked_object(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.Object");
    if (!ud->object || (ud->token && (!find_object(ud->object) || find_object(ud->object)->token != ud->token)))
        luaL_error(L, "stale Object reference");
    return ud->object;
}

static bool object_number_get(struct Object *o, const char *key, lua_State *L) {
#define OGET_NUM(name, field) if (!strcmp(key, name)) { lua_pushnumber(L, o->field); return true; }
#define OGET_INT(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, o->field); return true; }
    OGET_NUM("oPosX", oPosX) OGET_NUM("oPosY", oPosY) OGET_NUM("oPosZ", oPosZ)
    OGET_NUM("oVelX", oVelX) OGET_NUM("oVelY", oVelY) OGET_NUM("oVelZ", oVelZ)
    OGET_NUM("oForwardVel", oForwardVel) OGET_NUM("oDrawingDistance", oDrawingDistance)
    OGET_NUM("hitboxRadius", hitboxRadius) OGET_NUM("hitboxHeight", hitboxHeight)
    OGET_NUM("hurtboxRadius", hurtboxRadius) OGET_NUM("hurtboxHeight", hurtboxHeight)
    OGET_INT("oAction", oAction) OGET_INT("oSubAction", oSubAction) OGET_INT("oTimer", oTimer)
    OGET_INT("oFlags", oFlags) OGET_INT("oInteractType", oInteractType)
    OGET_INT("oBehParams", oBehParams) OGET_INT("oMoveAngleYaw", oMoveAngleYaw)
    OGET_INT("oFaceAngleYaw", oFaceAngleYaw) OGET_INT("activeFlags", activeFlags)
#undef OGET_NUM
#undef OGET_INT
    return false;
}

static bool object_number_set(struct Object *o, const char *key, lua_State *L, int i) {
#define OSET_NUM(name, field) if (!strcmp(key, name)) { o->field = (float)luaL_checknumber(L, i); return true; }
#define OSET_INT(name, field) if (!strcmp(key, name)) { o->field = (int32_t)luaL_checkinteger(L, i); return true; }
    OSET_NUM("oPosX", oPosX) OSET_NUM("oPosY", oPosY) OSET_NUM("oPosZ", oPosZ)
    OSET_NUM("oVelX", oVelX) OSET_NUM("oVelY", oVelY) OSET_NUM("oVelZ", oVelZ)
    OSET_NUM("oForwardVel", oForwardVel) OSET_NUM("oDrawingDistance", oDrawingDistance)
    OSET_NUM("hitboxRadius", hitboxRadius) OSET_NUM("hitboxHeight", hitboxHeight)
    OSET_NUM("hurtboxRadius", hurtboxRadius) OSET_NUM("hurtboxHeight", hurtboxHeight)
    OSET_INT("oAction", oAction) OSET_INT("oSubAction", oSubAction) OSET_INT("oTimer", oTimer)
    OSET_INT("oFlags", oFlags) OSET_INT("oInteractType", oInteractType)
    OSET_INT("oBehParams", oBehParams) OSET_INT("oMoveAngleYaw", oMoveAngleYaw)
    OSET_INT("oFaceAngleYaw", oFaceAngleYaw) OSET_INT("activeFlags", activeFlags)
#undef OSET_NUM
#undef OSET_INT
    return false;
}

static int object_index(lua_State *L) {
    struct Object *o = checked_object(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "parentObj") && o->parentObj) push_object(L, o->parentObj);
    else if (!strcmp(key, "prevObj") && o->prevObj) push_object(L, o->prevObj);
    else if (!strcmp(key, "oFaceAnglePitch")) lua_pushinteger(L, o->oFaceAnglePitch);
    else if (!strcmp(key, "oFaceAngleRoll")) lua_pushinteger(L, o->oFaceAngleRoll);
    else if (!object_number_get(o, key, L)) return luaL_error(L, "Object field '%s' is unsupported by this adapter", key);
    return 1;
}

static int object_newindex(lua_State *L) {
    struct Object *o = checked_object(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "oFaceAnglePitch")) o->oFaceAnglePitch = (s16)luaL_checkinteger(L, 3);
    else if (!strcmp(key, "oFaceAngleRoll")) o->oFaceAngleRoll = (s16)luaL_checkinteger(L, 3);
    else if (!object_number_set(o, key, L, 3)) return luaL_error(L, "Object field '%s' is read-only or unsupported", key);
    return 0;
}

static int l_hook_event(lua_State *L) {
    int event = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (event < 0 || event >= MOD_EVENT_COUNT) return luaL_error(L, "unknown CoopDX hook event id %d", event);
    if (event != HOOK_UPDATE && event != HOOK_MARIO_UPDATE && event != HOOK_BEFORE_MARIO_UPDATE
        && event != HOOK_ON_SET_MARIO_ACTION && event != HOOK_ON_HUD_RENDER
        && event != HOOK_ON_LEVEL_INIT && event != 30 && event != HOOK_ON_EXIT)
        return luaL_error(L, "CoopDX hook event %d is not supported by the current engine adapter", event);
    if (!sCurrentMod) return luaL_error(L, "hook_event must be called while loading a mod");
    lua_pushvalue(L, 2);
    if (!add_hook(sCurrentMod, event, luaL_ref(L, LUA_REGISTRYINDEX)))
        return luaL_error(L, "out of memory registering hook_event callback");
    return 0;
}

static int l_allocate_mario_action(lua_State *L) {
    uint32_t flags = (uint32_t)luaL_optinteger(L, 1, 0);
    if (!sCurrentMod) return luaL_error(L, "allocate_mario_action must be called while loading a mod");
    unsigned slot;
    for (slot = 0; slot < MOD_ACTION_ID_CAPACITY && sActionSlots[slot]; ++slot) {}
    if (slot == MOD_ACTION_ID_CAPACITY) return luaL_error(L, "custom Mario action id capacity exhausted");
    ModCustomActionId *allocated = calloc(1, sizeof(*allocated));
    if (!allocated) return luaL_error(L, "out of memory allocating a custom Mario action");
    sActionSlots[slot] = true;
    allocated->owner = sCurrentMod;
    allocated->slot = slot;
    allocated->id = (flags & ~ACT_ID_MASK) | MOD_ACTION_ID_BASE | slot;
    allocated->next_owned = sCurrentMod->custom_action_ids;
    sCurrentMod->custom_action_ids = allocated;
    uint32_t id = allocated->id;
    lua_pushinteger(L, id);
    return 1;
}

static int l_hook_mario_action(lua_State *L) {
    uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!sCurrentMod) return luaL_error(L, "hook_mario_action must be called while loading a mod");
    int every = callback_ref_from_table(L, 2, "every_frame");
    int gravity = callback_ref_from_table(L, 2, "gravity");
    if (every == LUA_NOREF && gravity == LUA_NOREF) return luaL_error(L, "action hook requires every_frame or gravity function");
    if (!add_action(sCurrentMod, id, every, gravity))
        return luaL_error(L, "out of memory registering Mario action callbacks");
    return 0;
}

static int l_hook_behavior(lua_State *L) {
    if (!lua_isnil(L, 1)) return luaL_error(L, "hook_behavior override of native behavior IDs is unsupported by this engine adapter");
    int list = (int)luaL_checkinteger(L, 2);
    (void)lua_toboolean(L, 3); /* replaceBehavior is meaningful only when replacing native behavior */
    luaL_checktype(L, 4, LUA_TFUNCTION);
    luaL_checktype(L, 5, LUA_TFUNCTION);
    if (!sCurrentMod) return luaL_error(L, "hook_behavior must be called while loading a mod");
    if (list < 0 || list >= NUM_OBJ_LISTS || list == OBJ_LIST_PLAYER)
        return luaL_error(L, "object list %d is unsupported for script behaviors", list);
    ModBehavior *behavior = calloc(1, sizeof(*behavior));
    if (!behavior) return luaL_error(L, "out of memory registering behavior");
    behavior->file = strdup(sCurrentFile ? sCurrentFile : "main.lua");
    if (!behavior->file) { free(behavior); return luaL_error(L, "out of memory registering behavior source"); }
    behavior->owner = sCurrentMod; behavior->id = MOD_DYNAMIC_ID_BASE | sNextBehaviorId++;
    behavior->object_list = list; behavior->enabled = true;
    lua_pushvalue(L, 4); behavior->init_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 5); behavior->loop_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    behavior->next_global = sBehaviors; sBehaviors = behavior;
    behavior->next_owned = sCurrentMod->behaviors; sCurrentMod->behaviors = behavior;
    lua_pushinteger(L, behavior->id);
    return 1;
}

static ModBehavior *find_behavior(uint32_t id) {
    for (ModBehavior *b = sBehaviors; b; b = b->next_global) if (b->id == id && b->enabled) return b;
    return NULL;
}

static ModObject *track_object(struct Object *object, ModBehavior *behavior) {
    ModObject *record = calloc(1, sizeof(*record));
    if (!record) return NULL;
    record->object = object; record->behavior = behavior; record->token = sNextObjectToken++;
    record->enabled = true; record->next = sObjects; sObjects = record;
    return record;
}

static struct Object *lua_spawn(lua_State *L, struct Object *parent, int model, uint32_t behavior_id, bool absolute,
                               float x, float y, float z, int pitch, int yaw, int roll) {
    if (model < 0 || model >= 0x100) {
        luaL_error(sLua, "model id %d is outside the native engine model table; DynOS model ids are unsupported", model);
        return NULL;
    }
    ModBehavior *behavior = find_behavior(behavior_id);
    if (!behavior) return NULL;
    const BehaviorScript *script = (const BehaviorScript *)mod_runtime_behavior_for_list(behavior->object_list);
    struct Object *object = absolute
        ? spawn_object_abs_with_rot(parent, 0, (s16)model, script, (s16)x, (s16)y, (s16)z,
                                    (s16)pitch, (s16)yaw, (s16)roll)
        : spawn_object(parent, model, script);
    if (object && !track_object(object, behavior)) {
        object->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        luaL_error(L, "out of memory tracking mod-owned object lifecycle");
        return NULL;
    }
    return object;
}

static int l_spawn_object(lua_State *L) {
    struct Object *parent = lua_isnoneornil(L, 1) ? (gMarioState ? gMarioState->marioObj : NULL) : checked_object(L, 1);
    int model = (int)luaL_checkinteger(L, 2);
    uint32_t behavior_id = (uint32_t)luaL_checkinteger(L, 3);
    struct Object *object = lua_spawn(L, parent, model, behavior_id, false, 0, 0, 0, 0, 0, 0);
    if (!object) { lua_pushnil(L); return 1; }
    push_object(L, object);
    return 1;
}

static int l_spawn_object_abs_with_rot(lua_State *L) {
    struct Object *parent = lua_isnoneornil(L, 1) ? (gMarioState ? gMarioState->marioObj : NULL) : checked_object(L, 1);
    int spawn_type = (int)luaL_optinteger(L, 2, 0); (void)spawn_type;
    int model = (int)luaL_checkinteger(L, 3);
    uint32_t behavior_id = (uint32_t)luaL_checkinteger(L, 4);
    struct Object *object = lua_spawn(L, parent, model, behavior_id, true,
        (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
        (int)luaL_optinteger(L, 8, 0), (int)luaL_optinteger(L, 9, 0), (int)luaL_optinteger(L, 10, 0));
    if (!object) { lua_pushnil(L); return 1; }
    push_object(L, object);
    return 1;
}

static int l_set_mario_action(lua_State *L) {
    struct MarioState *m = current_mario();
    if (!m) return luaL_error(L, "Mario state is unavailable");
    uint32_t action = (uint32_t)luaL_checkinteger(L, 2);
    uint32_t arg = (uint32_t)luaL_optinteger(L, 3, 0);
    uint32_t result = set_mario_action(m, action, arg);
    lua_pushinteger(L, result);
    return 1;
}

static int l_mario_set_forward_vel(lua_State *L) {
    struct MarioState *m = current_mario();
    if (!m) return 0;
    m->forwardVel = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_set_mario_animation(lua_State *L) {
    struct MarioState *m = current_mario();
    if (!m) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, set_mario_animation(m, (s32)luaL_checkinteger(L, 2)));
    return 1;
}

static int l_global_missing(lua_State *L) {
    const char *name = lua_tostring(L, 2);
    if (name && (!strncmp(name, "djui_", 5) || !strncmp(name, "network_", 8)
                 || !strncmp(name, "smlua_", 6) || !strncmp(name, "spawn_sync_", 11)
                 || !strncmp(name, "hook_", 5) || !strncmp(name, "character_select_", 17)
                 || !strncmp(name, "perform_", 8) || !strncmp(name, "cur_obj_", 8)
                 || !strncmp(name, "mario_", 6) || !strncmp(name, "play_", 5)
                 || !strncmp(name, "set_", 4) || !strncmp(name, "obj_", 4)
                 || !strncmp(name, "synchronize_", 12) || !strncmp(name, "register_", 9)
                 || !strncmp(name, "texture_", 8) || !strncmp(name, "audio_", 6)
                 || !strncmp(name, "sound_", 6) || !strncmp(name, "sfx_", 4)
                 || !strncmp(name, "level_", 6) || !strncmp(name, "geo_", 4)
                 || !strncmp(name, "gfx_", 4) || !strncmp(name, "find_", 5)
                 || !strncmp(name, "load_", 5) || !strncmp(name, "save_", 5)
                 || !strncmp(name, "create_", 7) || !strncmp(name, "get_", 4)
                 || !strncmp(name, "ACT_", 4) || !strncmp(name, "INPUT_", 6)
                 || !strncmp(name, "MODEL_", 6) || !strncmp(name, "MARIO_ANIM_", 11)
                 || !strncmp(name, "SOUND_", 6) || !strncmp(name, "PARTICLE_", 9)
                 || !strncmp(name, "INTERACT_", 9) || !strncmp(name, "SURFACE_", 8)
                 || !strncmp(name, "DIALOG_", 7) || !strncmp(name, "SEQ_", 4)
                 || !strncmp(name, "BUTTON_", 7) || !strncmp(name, "CONTROLLER_", 11)
                 || !strcmp(name, "gNetworkPlayers") || !strcmp(name, "gPlayerSyncTable")
                 || !strcmp(name, "gGlobalSyncTable") || !strcmp(name, "gActiveMods")
                 || !strcmp(name, "gCharacters"))) {
        lua_pushfstring(L, "unsupported CoopDX API '%s' (missing engine capability)", name);
        return luaL_error(L, "%s", lua_tostring(L, -1));
    }
    lua_pushnil(L);
    return 1;
}

static char *normalize_require_name(const char *name, const char *from_file) {
    size_t name_len = strlen(name);
    if (!name_len || name_len > 240 || name[0] == '/' || name[0] == '\\') return NULL;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p == ':' || *p == '\\' || *p < 0x20) return NULL;

    char combined[482];
    size_t base_len = 0;
    if (from_file) {
        const char *last_slash = strrchr(from_file, '/');
        if (last_slash) base_len = (size_t)(last_slash - from_file);
    }
    if (base_len + (base_len ? 1 : 0) + name_len >= sizeof(combined)) return NULL;
    if (base_len) memcpy(combined, from_file, base_len);
    size_t combined_len = base_len;
    if (base_len) combined[combined_len++] = '/';
    memcpy(combined + combined_len, name, name_len + 1);

    char *result = malloc(241);
    size_t segment_starts[121];
    if (!result) return NULL;
    size_t output_len = 0;
    size_t depth = 0;
    const char *part = combined;
    for (const char *p = combined;; ++p) {
        if (*p != '/' && *p != 0) continue;
        size_t part_len = (size_t)(p - part);
        if (!part_len) { free(result); return NULL; }
        if (part_len == 1 && part[0] == '.') {
            /* Current-directory segment. */
        } else if (part_len == 2 && part[0] == '.' && part[1] == '.') {
            if (!depth) { free(result); return NULL; }
            output_len = segment_starts[--depth];
        } else {
            if (depth >= sizeof(segment_starts) / sizeof(segment_starts[0])
                || output_len + (output_len ? 1 : 0) + part_len > 240) {
                free(result); return NULL;
            }
            segment_starts[depth++] = output_len;
            if (output_len) result[output_len++] = '/';
            memcpy(result + output_len, part, part_len);
            output_len += part_len;
        }
        if (!*p) break;
        part = p + 1;
    }
    if (!output_len) { free(result); return NULL; }
    result[output_len] = 0;
    return result;
}

static ModModule *find_module(Mod *mod, const char *name) {
    for (ModModule *module = mod->modules; module; module = module->next)
        if (!strcmp(module->name, name)) return module;
    return NULL;
}

static int load_mod_file(Mod *mod, const char *relative, bool return_module);
static void unload_mod(Mod *mod);

static int l_mod_require(lua_State *L) {
    Mod *mod = sCurrentMod;
    const char *request = luaL_checkstring(L, 1);
    if (!mod) return luaL_error(L, "require() is only available while a mod is executing");
    char *normalized = normalize_require_name(request, sCurrentFile);
    if (!normalized) return luaL_error(L, "invalid module path '%s'", request);
    ModModule *cached = find_module(mod, normalized);
    if (cached) {
        free(normalized);
        if (cached->loading) lua_pushboolean(L, 1);
        else lua_rawgeti(L, LUA_REGISTRYINDEX, cached->value_ref);
        return 1;
    }
    char module_key[241];
    snprintf(module_key, sizeof(module_key), "%s", normalized);
    char relative[SYS_MAX_PATH];
    snprintf(relative, sizeof(relative), "%s.lua", normalized);
    if (!safe_relative_path(relative)) { free(normalized); return luaL_error(L, "invalid module path"); }
    char path[SYS_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", mod->path, relative);
    if (!fs_sys_file_exists(path)) {
        snprintf(relative, sizeof(relative), "%s.luac", normalized);
        snprintf(path, sizeof(path), "%s/%s", mod->path, relative);
    }
    free(normalized);
    if (!safe_relative_path(relative)) return luaL_error(L, "invalid module path");
    if (!fs_sys_file_exists(path)) return luaL_error(L, "module not found inside mod '%s': %s", mod->name, relative);
    cached = calloc(1, sizeof(*cached));
    if (!cached) return luaL_error(L, "out of memory caching module");
    cached->name = strdup(module_key);
    if (!cached->name) { free(cached); return luaL_error(L, "out of memory caching module"); }
    cached->value_ref = LUA_NOREF; cached->loading = true; cached->next = mod->modules; mod->modules = cached;
    if (!load_mod_file(mod, relative, true)) {
        cached->loading = false;
        return luaL_error(L, "failed loading module '%s'", request);
    }
    cached->loading = false;
    cached->value_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_rawgeti(L, LUA_REGISTRYINDEX, cached->value_ref);
    return 1;
}

static bool set_mod_environment(lua_State *L, int function_index, Mod *mod) {
    function_index = lua_absindex(L, function_index);
    if (mod->env_ref == LUA_NOREF) {
        lua_newtable(L);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "_G");
        lua_newtable(L);
        lua_pushglobaltable(L); lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        mod->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->env_ref);
    const char *upvalue = lua_setupvalue(L, function_index, 1);
    return upvalue != NULL;
}

static int load_mod_file(Mod *mod, const char *relative, bool return_module) {
    char fullpath[SYS_MAX_PATH];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", mod->path, relative);
    FILE *file = fopen(fullpath, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) || ftell(file) < 0) { fclose(file); return 0; }
    long size = ftell(file);
    if (size <= 0 || size > 2 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return 0; }
    char *buffer = malloc((size_t)size);
    if (!buffer) { fclose(file); return 0; }
    size_t got = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (got != (size_t)size) { free(buffer); return 0; }
    /* CoopDX accepts both source and Lua 5.3 bytecode. Lua validates the
     * bytecode header; incompatible Lua versions/architectures fail closed. */
    int status = luaL_loadbufferx(sLua, buffer, (size_t)size, fullpath, "bt");
    free(buffer);
    if (status != LUA_OK) {
        mod_logf("error", mod->name, "compile error in %s: %s", relative, lua_tostring(sLua, -1));
        lua_pop(sLua, 1); return 0;
    }
    if (!set_mod_environment(sLua, -1, mod)) { lua_pop(sLua, 1); return 0; }
    Mod *previous_mod = sCurrentMod;
    const char *previous_file = sCurrentFile;
    sCurrentMod = mod; sCurrentFile = relative;
    begin_lua_callback(sLua);
    status = lua_pcall(sLua, 0, return_module ? 1 : 0, 0);
    end_lua_callback();
    sCurrentMod = previous_mod; sCurrentFile = previous_file;
    if (status != LUA_OK) {
        mod_logf("error", mod->name, "runtime error in %s: %s", relative, lua_tostring(sLua, -1));
        lua_pop(sLua, 1); ++sHookFailures; return 0;
    }
    if (return_module) {
        if (lua_isnil(sLua, -1)) { lua_pop(sLua, 1); lua_pushboolean(sLua, 1); }
        return 1;
    }
    return 1;
}

static void register_api_function(const char *name, lua_CFunction fn) {
    lua_pushcfunction(sLua, fn); lua_setglobal(sLua, name);
}

static void register_metatable(const char *name, lua_CFunction index, lua_CFunction newindex) {
    luaL_newmetatable(sLua, name);
    lua_pushcfunction(sLua, index); lua_setfield(sLua, -2, "__index");
    if (newindex) { lua_pushcfunction(sLua, newindex); lua_setfield(sLua, -2, "__newindex"); }
    lua_pop(sLua, 1);
}

static void expose_int(const char *name, lua_Integer value) {
    lua_pushinteger(sLua, value); lua_setglobal(sLua, name);
}

static void register_api(void) {
    register_metatable("sm64.mod.MarioState", mario_index, mario_newindex);
    register_metatable("sm64.mod.Controller", controller_index, controller_newindex);
    register_metatable("sm64.mod.Vec3f", vector_index, vector_newindex);
    register_metatable("sm64.mod.Vec3s", vector_index, vector_newindex);
    register_metatable("sm64.mod.Object", object_index, object_newindex);
    register_api_function("hook_event", l_hook_event);
    register_api_function("allocate_mario_action", l_allocate_mario_action);
    register_api_function("hook_mario_action", l_hook_mario_action);
    register_api_function("hook_behavior", l_hook_behavior);
    register_api_function("spawn_object", l_spawn_object);
    register_api_function("spawn_object_abs_with_rot", l_spawn_object_abs_with_rot);
    register_api_function("set_mario_action", l_set_mario_action);
    register_api_function("mario_set_forward_vel", l_mario_set_forward_vel);
    register_api_function("set_mario_animation", l_set_mario_animation);
    register_api_function("require", l_mod_require);

    expose_int("HOOK_UPDATE", HOOK_UPDATE);
    expose_int("HOOK_MARIO_UPDATE", HOOK_MARIO_UPDATE);
    expose_int("HOOK_BEFORE_MARIO_UPDATE", HOOK_BEFORE_MARIO_UPDATE);
    expose_int("HOOK_ON_SET_MARIO_ACTION", HOOK_ON_SET_MARIO_ACTION);
    expose_int("HOOK_BEFORE_PHYS_STEP", HOOK_BEFORE_PHYS_STEP);
    expose_int("HOOK_BEFORE_SET_MARIO_ACTION", 30);
    expose_int("HOOK_ON_HUD_RENDER", HOOK_ON_HUD_RENDER);
    expose_int("HOOK_ALLOW_INTERACT", HOOK_ALLOW_INTERACT);
    expose_int("HOOK_ON_INTERACT", HOOK_ON_INTERACT);
    expose_int("HOOK_ON_LEVEL_INIT", HOOK_ON_LEVEL_INIT);
    expose_int("HOOK_ON_EXIT", HOOK_ON_EXIT);

    expose_int("OBJ_LIST_PLAYER", OBJ_LIST_PLAYER); expose_int("OBJ_LIST_GENACTOR", OBJ_LIST_GENACTOR);
    expose_int("OBJ_LIST_PUSHABLE", OBJ_LIST_PUSHABLE); expose_int("OBJ_LIST_LEVEL", OBJ_LIST_LEVEL);
    expose_int("OBJ_LIST_DEFAULT", OBJ_LIST_DEFAULT); expose_int("OBJ_LIST_SURFACE", OBJ_LIST_SURFACE);
    expose_int("OBJ_LIST_POLELIKE", OBJ_LIST_POLELIKE); expose_int("OBJ_LIST_SPAWNER", OBJ_LIST_SPAWNER);
    expose_int("OBJ_LIST_UNIMPORTANT", OBJ_LIST_UNIMPORTANT);

    expose_int("ACT_FLAG_AIR", ACT_FLAG_AIR); expose_int("ACT_FLAG_MOVING", ACT_FLAG_MOVING);
    expose_int("ACT_FLAG_STATIONARY", ACT_FLAG_STATIONARY); expose_int("ACT_FLAG_INTANGIBLE", ACT_FLAG_INTANGIBLE);
    expose_int("INPUT_A_PRESSED", INPUT_A_PRESSED); expose_int("INPUT_A_DOWN", INPUT_A_DOWN);
    expose_int("INPUT_B_PRESSED", INPUT_B_PRESSED);
    expose_int("INPUT_Z_PRESSED", INPUT_Z_PRESSED); expose_int("INPUT_Z_DOWN", INPUT_Z_DOWN);
    expose_int("PARTICLE_DUST", PARTICLE_DUST);
    expose_int("ACT_IDLE", ACT_IDLE); expose_int("ACT_JUMP", ACT_JUMP); expose_int("ACT_FREEFALL", ACT_FREEFALL);
    expose_int("ACT_WALKING", ACT_WALKING); expose_int("ACT_DOUBLE_JUMP", ACT_DOUBLE_JUMP);
    expose_int("ACT_TRIPLE_JUMP", ACT_TRIPLE_JUMP); expose_int("ACT_LONG_JUMP", ACT_LONG_JUMP);
    expose_int("ACT_GROUND_POUND", ACT_GROUND_POUND); expose_int("ACT_SLIDE_KICK", ACT_SLIDE_KICK);

#include "mod_constants.inc"

    lua_newtable(sLua);
    push_mario(sLua); lua_rawseti(sLua, -2, 0);
    lua_setglobal(sLua, "gMarioStates");
    push_mario(sLua); lua_setglobal(sLua, "gMarioState");

    lua_pushglobaltable(sLua);
    lua_newtable(sLua); lua_pushcfunction(sLua, l_global_missing); lua_setfield(sLua, -2, "__index");
    lua_setmetatable(sLua, -2); lua_pop(sLua, 1);
    /* The base library includes disk-loading helpers; only memory source load remains. */
    lua_pushnil(sLua); lua_setglobal(sLua, "dofile");
    lua_pushnil(sLua); lua_setglobal(sLua, "loadfile");
}

static void open_safe_libraries(lua_State *L) {
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
}

static bool load_mod(Mod *mod) {
    if (!mod || mod->loaded || mod->load_failed) return mod && mod->loaded;
    if (!mod->file_count) {
        mod->load_failed = true;
        mod_log("warning", mod->name, "mod contains no Lua scripts");
        return false;
    }
    mod->loaded = true;
    /* CoopDX auto-runs root-level scripts in alphabetical path order. Nested
     * scripts stay available to require(), but are not startup entry points. */
    bool root_mod = !strcmp(mod->path, fs_get_write_path("mods"));
    bool found_main = root_mod;
    for (size_t i = 0; i < mod->file_count; ++i) {
        const char *relative = relative_path(mod, mod->files[i]);
        if (relative && !strcmp(relative, "main.lua")) found_main = true;
    }
    if (!found_main) {
        mod->loaded = false;
        mod->load_failed = true;
        mod_log("warning", mod->name, "mod folder has no root-level main.lua");
        return false;
    }
    for (size_t i = 0; i < mod->file_count; ++i) {
        const char *relative = relative_path(mod, mod->files[i]);
        if (!relative || strchr(relative, '/')) continue;
        if (!load_mod_file(mod, relative, false)) {
            mod_log("error", mod->name, "startup script failed; discarding registrations created by this mod");
            unload_mod(mod);
            mod->load_failed = true;
            return false;
        }
    }
    if (mod->contains_dynos_assets) {
        mod_log("warning", mod->name,
                "package contains DynOS assets; import preserved them, but this engine has no DynOS actor decoder yet");
    }
    mod_log("info", mod->name, "mod scripts loaded");
    return true;
}

static void unref_if_valid(int ref) { if (ref != LUA_NOREF && ref != LUA_REFNIL && sLua) luaL_unref(sLua, LUA_REGISTRYINDEX, ref); }
static void release_ref(int *ref) {
    if (!ref) return;
    unref_if_valid(*ref);
    *ref = LUA_NOREF;
}

static void unload_mod(Mod *mod) {
    if (!mod) return;

    /* Stop scripted objects before freeing the callbacks they reference. */
    ModObject **object_link = &sObjects;
    while (*object_link) {
        ModObject *object = *object_link;
        if (object->behavior && object->behavior->owner == mod) {
            if (object->object) object->object->activeFlags = ACTIVE_FLAG_DEACTIVATED;
            *object_link = object->next;
            free(object);
        } else object_link = &object->next;
    }

    ModHook **hook_link = &sHooks;
    while (*hook_link) {
        ModHook *hook = *hook_link;
        if (hook->owner == mod) {
            *hook_link = hook->next_global;
            release_ref(&hook->function_ref);
            free(hook->file);
            free(hook);
        } else hook_link = &hook->next_global;
    }
    mod->hooks = NULL;

    ModAction **action_link = &sActions;
    while (*action_link) {
        ModAction *action = *action_link;
        if (action->owner == mod) {
            *action_link = action->next_global;
            release_ref(&action->every_frame_ref);
            release_ref(&action->gravity_ref);
            free(action->file);
            free(action);
        } else action_link = &action->next_global;
    }
    mod->actions = NULL;

    ModBehavior **behavior_link = &sBehaviors;
    while (*behavior_link) {
        ModBehavior *behavior = *behavior_link;
        if (behavior->owner == mod) {
            *behavior_link = behavior->next_global;
            release_ref(&behavior->init_ref);
            release_ref(&behavior->loop_ref);
            free(behavior->file);
            free(behavior);
        } else behavior_link = &behavior->next_global;
    }
    mod->behaviors = NULL;
    while (mod->custom_action_ids) {
        ModCustomActionId *next = mod->custom_action_ids->next_owned;
        sActionSlots[mod->custom_action_ids->slot] = false;
        free(mod->custom_action_ids);
        mod->custom_action_ids = next;
    }
    unref_if_valid(mod->env_ref); mod->env_ref = LUA_NOREF;
    while (mod->modules) {
        ModModule *next = mod->modules->next;
        unref_if_valid(mod->modules->value_ref);
        free(mod->modules->name); free(mod->modules); mod->modules = next;
    }
    mod->loaded = false;
}

static void call_event(int event, int nargs, bool push_mario_state, int extra_ints) {
    if (!sLua) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != event) continue;
        if (push_mario_state) push_mario(sLua);
        for (int i = 0; i < extra_ints; ++i) lua_pushinteger(sLua, 0);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "hook_event", nargs, 0)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
}

bool mod_runtime_init(void) {
    if (sInitialized) return true;
    sLua = lua_newstate(lua_mod_alloc, &sAllocator);
    if (!sLua) { mod_log("error", NULL, "could not allocate Lua state within 32 MiB budget"); return false; }
    /* lua_newthread inherits its parent's hook in the vendored Lua 5.3 build;
     * keeping this installed makes coroutine.resume/wrap obey the same limit. */
    lua_sethook(sLua, lua_instruction_budget, LUA_MASKCOUNT, MOD_INSTRUCTION_QUANTUM);
    open_safe_libraries(sLua);
    register_api();
    discover_mods();
    sInitialized = true;
    return true;
}

static bool enqueue_request(int type, int index, bool enabled, const char *path) {
    ModRequest *request = calloc(1, sizeof(*request));
    if (!request) return false;
    request->type = type; request->index = index; request->enabled = enabled;
    if (type == MOD_REQUEST_ENABLE || type == MOD_REQUEST_RELOAD) {
        if (index < 0) { free(request); return false; }
        pthread_mutex_lock(&sModListMutex);
        Mod *target = sMods;
        for (int at = index; target && at > 0; --at) target = target->next;
        if (target) snprintf(request->target_name, sizeof(request->target_name), "%s", target->name);
        pthread_mutex_unlock(&sModListMutex);
        if (!request->target_name[0]) { free(request); return false; }
    }
    if (path) {
        request->path = strdup(path);
        if (!request->path) { free(request); return false; }
    }
    pthread_mutex_lock(&sRequestMutex);
    unsigned count = 0;
    for (ModRequest *at = sRequestHead; at; at = at->next) ++count;
    if (count >= 64) {
        pthread_mutex_unlock(&sRequestMutex);
        free(request->path); free(request); return false;
    }
    if (sRequestTail) sRequestTail->next = request;
    else sRequestHead = request;
    sRequestTail = request;
    pthread_mutex_unlock(&sRequestMutex);
    return true;
}

bool mod_runtime_request_enabled(int index, bool enabled) {
    return index >= 0 && enqueue_request(MOD_REQUEST_ENABLE, index, enabled, NULL);
}

bool mod_runtime_request_reload(int index) {
    return index >= 0 && enqueue_request(MOD_REQUEST_RELOAD, index, false, NULL);
}

bool mod_runtime_request_import_zip(const char *zip_path) {
    if (!zip_path || !*zip_path) return false;
    return enqueue_request(MOD_REQUEST_IMPORT_ZIP, 0, false, zip_path);
}

static void process_requests(void) {
    for (;;) {
        pthread_mutex_lock(&sRequestMutex);
        ModRequest *request = sRequestHead;
        if (request) {
            sRequestHead = request->next;
            if (!sRequestHead) sRequestTail = NULL;
        }
        pthread_mutex_unlock(&sRequestMutex);
        if (!request) break;
        bool ok = false;
        if (request->type == MOD_REQUEST_ENABLE || request->type == MOD_REQUEST_RELOAD) {
            Mod *target = find_mod(request->target_name);
            if (target && request->type == MOD_REQUEST_ENABLE) {
                pthread_mutex_lock(&sModListMutex);
                bool was_enabled = target->enabled;
                target->enabled = request->enabled;
                pthread_mutex_unlock(&sModListMutex);
                if (was_enabled == request->enabled) ok = true;
                else if (request->enabled) {
                    target->load_failed = false;
                    ok = load_mod(target);
                    if (!ok) {
                        pthread_mutex_lock(&sModListMutex); target->enabled = false; pthread_mutex_unlock(&sModListMutex);
                    }
                } else {
                    unload_mod(target);
                    ok = true;
                }
            } else if (target) {
                pthread_mutex_lock(&sModListMutex);
                bool was_enabled = target->enabled;
                pthread_mutex_unlock(&sModListMutex);
                unload_mod(target); target->load_failed = false;
                ok = !was_enabled || load_mod(target);
                if (!ok) {
                    pthread_mutex_lock(&sModListMutex); target->enabled = false; pthread_mutex_unlock(&sModListMutex);
                }
            }
        }
        else if (request->type == MOD_REQUEST_IMPORT_ZIP) {
            char installed[SYS_MAX_PATH], error[512];
            ok = mod_runtime_import_zip(request->path, installed, sizeof(installed), error, sizeof(error));
            if (ok && error[0]) mod_logf("warning", NULL, "imported archive with compatibility limits: %s", error);
            else if (ok) mod_logf("info", NULL, "imported mod archive: %s", installed);
            else mod_logf("error", NULL, "mod archive import failed: %s", error);
            unlink(request->path);
        }
        if (!ok && request->type != MOD_REQUEST_IMPORT_ZIP)
            mod_log("warning", NULL, "queued mod management request could not be applied");
        free(request->path); free(request);
    }
}

void mod_runtime_update(void) {
    if (!sModsLoaded) {
        for (Mod *mod = sMods; mod; mod = mod->next) if (mod->enabled && !load_mod(mod)) {
            pthread_mutex_lock(&sModListMutex); mod->enabled = false; pthread_mutex_unlock(&sModListMutex);
        }
        sModsLoaded = true;
    }
    process_requests();
    call_event(HOOK_UPDATE, 0, false, 0);
}
void mod_runtime_before_mario_update(struct MarioState *m) { (void)m; call_event(HOOK_BEFORE_MARIO_UPDATE, 1, true, 0); }
void mod_runtime_after_mario_update(struct MarioState *m) { (void)m; call_event(HOOK_MARIO_UPDATE, 1, true, 0); }

void mod_runtime_before_set_action(struct MarioState *m, uint32_t *action, uint32_t action_arg, bool *cancel) {
    (void)m;
    if (!sLua) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != 30) continue; /* upstream HOOK_BEFORE_SET_MARIO_ACTION */
        push_mario(sLua); lua_pushinteger(sLua, *action); lua_pushinteger(sLua, action_arg);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_BEFORE_SET_MARIO_ACTION", 3, 1)) {
            hook->enabled = false; release_ref(&hook->function_ref); continue;
        }
        if (lua_isinteger(sLua, -1)) {
            uint32_t result = (uint32_t)lua_tointeger(sLua, -1);
            if (result == 1) *cancel = true;
            else *action = result;
        }
        lua_pop(sLua, 1);
    }
}

void mod_runtime_after_set_action(struct MarioState *m) {
    (void)m; call_event(HOOK_ON_SET_MARIO_ACTION, 1, true, 0);
}

bool mod_runtime_mario_action(struct MarioState *m, int *in_loop) {
    if (!sLua || !m) return false;
    for (ModAction *action = sActions; action; action = action->next_global) {
        if (!action->enabled || !action->owner->enabled || action->id != m->action || action->every_frame_ref == LUA_NOREF) continue;
        push_mario(sLua);
        if (call_ref(action->owner, action->file, action->every_frame_ref, "hook_mario_action.every_frame", 1, 1)) {
            *in_loop = lua_toboolean(sLua, -1); lua_pop(sLua, 1);
        } else {
            action->enabled = false;
            release_ref(&action->every_frame_ref);
            *in_loop = FALSE;
            set_mario_action(m, ACT_IDLE, 0);
        }
        return true;
    }
    return false;
}

bool mod_runtime_mario_gravity(struct MarioState *m) {
    if (!sLua || !m) return false;
    for (ModAction *action = sActions; action; action = action->next_global) {
        if (!action->enabled || !action->owner->enabled || action->id != m->action || action->gravity_ref == LUA_NOREF) continue;
        push_mario(sLua);
        if (!call_ref(action->owner, action->file, action->gravity_ref, "hook_mario_action.gravity", 1, 0)) {
            release_ref(&action->gravity_ref);
            return false;
        }
        return true;
    }
    return false;
}

void mod_runtime_hud_render(void) { call_event(HOOK_ON_HUD_RENDER, 0, false, 0); }
void mod_runtime_level_init(int type, int level, int area, int node, int arg) {
    if (!sLua) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_LEVEL_INIT) continue;
        lua_pushinteger(sLua, type); lua_pushinteger(sLua, level); lua_pushinteger(sLua, area);
        lua_pushinteger(sLua, node); lua_pushinteger(sLua, arg);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ON_LEVEL_INIT", 5, 0)) {
            hook->enabled = false; release_ref(&hook->function_ref);
        }
    }
}

void mod_runtime_behavior_tick(void) {
    if (!sLua || !gCurrentObject) return;
    ModObject *entry = find_object(gCurrentObject);
    if (!entry || !entry->enabled || !entry->behavior || !entry->behavior->enabled) return;
    ModBehavior *behavior = entry->behavior;
    if (!entry->initialized) {
        push_object(sLua, gCurrentObject);
        if (!call_ref(behavior->owner, behavior->file, behavior->init_ref, "hook_behavior.init", 1, 0)) { entry->enabled = false; return; }
        entry->initialized = true;
    }
    push_object(sLua, gCurrentObject);
    if (!call_ref(behavior->owner, behavior->file, behavior->loop_ref, "hook_behavior.loop", 1, 0)) entry->enabled = false;
}

void mod_runtime_object_unload(struct Object *obj) {
    if (!obj) return;
    ModObject **link = &sObjects;
    while (*link) {
        ModObject *entry = *link;
        if (entry->object == obj) {
            *link = entry->next;
            free(entry);
        } else link = &entry->next;
    }
}

static void script_behavior_native(void) { mod_runtime_behavior_tick(); }
#define MOD_BHV_B(list) ((((uintptr_t)0x00) << 24) | (((uintptr_t)(list) & 0xFF) << 16))
#define MOD_BHV_CMD(op) (((uintptr_t)(op) & 0xFF) << 24)
#define MOD_BHV_SCRIPT(list) { MOD_BHV_B(list), MOD_BHV_CMD(0x08), MOD_BHV_CMD(0x0C), (uintptr_t)script_behavior_native, MOD_BHV_CMD(0x09) }
static const BehaviorScript sBehaviorScripts[NUM_OBJ_LISTS][5] = {
    [OBJ_LIST_UNUSED_1] = MOD_BHV_SCRIPT(OBJ_LIST_UNUSED_1),
    [OBJ_LIST_DESTRUCTIVE] = MOD_BHV_SCRIPT(OBJ_LIST_DESTRUCTIVE),
    [OBJ_LIST_UNUSED_3] = MOD_BHV_SCRIPT(OBJ_LIST_UNUSED_3),
    [OBJ_LIST_GENACTOR] = MOD_BHV_SCRIPT(OBJ_LIST_GENACTOR),
    [OBJ_LIST_PUSHABLE] = MOD_BHV_SCRIPT(OBJ_LIST_PUSHABLE),
    [OBJ_LIST_LEVEL] = MOD_BHV_SCRIPT(OBJ_LIST_LEVEL),
    [OBJ_LIST_UNUSED_7] = MOD_BHV_SCRIPT(OBJ_LIST_UNUSED_7),
    [OBJ_LIST_DEFAULT] = MOD_BHV_SCRIPT(OBJ_LIST_DEFAULT),
    [OBJ_LIST_SURFACE] = MOD_BHV_SCRIPT(OBJ_LIST_SURFACE),
    [OBJ_LIST_POLELIKE] = MOD_BHV_SCRIPT(OBJ_LIST_POLELIKE),
    [OBJ_LIST_SPAWNER] = MOD_BHV_SCRIPT(OBJ_LIST_SPAWNER),
    [OBJ_LIST_UNIMPORTANT] = MOD_BHV_SCRIPT(OBJ_LIST_UNIMPORTANT),
};
const uintptr_t *mod_runtime_behavior_for_list(int list) {
    if (list < 0 || list >= NUM_OBJ_LISTS || list == OBJ_LIST_PLAYER) return sBehaviorScripts[OBJ_LIST_DEFAULT];
    return sBehaviorScripts[list];
}

int mod_runtime_mod_count(void) {
    pthread_mutex_lock(&sModListMutex);
    int n = 0; for (Mod *m = sMods; m; m = m->next) ++n;
    pthread_mutex_unlock(&sModListMutex);
    return n;
}
const char *mod_runtime_mod_name(int index) {
    static char result[MOD_MAX_NAME];
    result[0] = 0;
    if (index < 0) return NULL;
    pthread_mutex_lock(&sModListMutex);
    for (Mod *m = sMods; m; m = m->next) if (!index--) { snprintf(result, sizeof(result), "%s", m->name); break; }
    pthread_mutex_unlock(&sModListMutex);
    return result[0] ? result : NULL;
}
bool mod_runtime_mod_enabled(int index) {
    if (index < 0) return false;
    pthread_mutex_lock(&sModListMutex);
    bool enabled = false;
    for (Mod *m = sMods; m; m = m->next) if (!index--) { enabled = m->enabled; break; }
    pthread_mutex_unlock(&sModListMutex);
    return enabled;
}

bool mod_runtime_set_enabled(int index, bool enabled) {
    if (index < 0) return false;
    for (Mod *mod = sMods; mod; mod = mod->next) if (!index--) {
        pthread_mutex_lock(&sModListMutex);
        bool was_enabled = mod->enabled;
        mod->enabled = enabled;
        pthread_mutex_unlock(&sModListMutex);
        if (was_enabled == enabled) return true;
        if (enabled) {
            mod->load_failed = false;
            bool ok = load_mod(mod);
            if (!ok) { pthread_mutex_lock(&sModListMutex); mod->enabled = false; pthread_mutex_unlock(&sModListMutex); }
            return ok;
        }
        unload_mod(mod); return true;
    }
    return false;
}

bool mod_runtime_reload(int index) {
    if (index < 0) return false;
    for (Mod *mod = sMods; mod; mod = mod->next) if (!index--) {
        bool was_enabled = mod->enabled;
        unload_mod(mod); mod->load_failed = false;
        bool ok = was_enabled ? load_mod(mod) : true;
        if (!ok) { pthread_mutex_lock(&sModListMutex); mod->enabled = false; pthread_mutex_unlock(&sModListMutex); }
        return ok;
    }
    return false;
}

bool mod_runtime_import_zip(const char *zip_path, char *installed_path, unsigned installed_path_size,
                            char *error, unsigned error_size) {
    char mods_root[SYS_MAX_PATH];
    char dynos_parent[SYS_MAX_PATH];
    char dynos_root[SYS_MAX_PATH];
    char installed[SYS_MAX_PATH];
    snprintf(mods_root, sizeof(mods_root), "%s/mods", fs_writepath);
    snprintf(dynos_parent, sizeof(dynos_parent), "%s/dynos", fs_writepath);
    snprintf(dynos_root, sizeof(dynos_root), "%s/dynos/packs", fs_writepath);
    bool contains_lua = false, contains_dynos = false;
    if ((!fs_sys_dir_exists(dynos_parent) && !fs_sys_mkdir(dynos_parent)) ||
        !fs_zip_extract_package(zip_path, mods_root, dynos_root, installed, sizeof(installed),
                                &contains_lua, &contains_dynos, error, error_size)) return false;
    if (installed_path && installed_path_size) snprintf(installed_path, installed_path_size, "%s", installed);
    const char *name = strrchr(installed, '/'); name = name ? name + 1 : installed;
    if (!contains_lua) {
        if (contains_dynos) add_dynos_pack(name, installed);
        if (error && error_size) snprintf(error, error_size,
            "DynOS pack imported, but model, animation, texture, and behavior decoding is not available in this engine yet");
        return contains_dynos;
    }
    add_mod(name, installed);
    Mod *mod = find_mod(name);
    if (!mod) {
        if (error && error_size) snprintf(error, error_size, "package installed but runtime could not allocate mod record");
        return true;
    }
    mod->files = NULL; mod->file_count = 0;
    mod->contains_dynos_assets = false;
    fs_sys_walk(mod->path, mod_file_walk, mod, true);
    qsort(mod->files, mod->file_count, sizeof(char *), compare_strings);
    if (sLua && !load_mod(mod)) {
        pthread_mutex_lock(&sModListMutex);
        mod->enabled = false;
        pthread_mutex_unlock(&sModListMutex);
        if (error && error_size) snprintf(error, error_size, "package imported; Lua script failed during loading (see mod diagnostics)");
    } else if (error && error_size) error[0] = 0;
    return true;
}

int mod_runtime_dynos_pack_count(void) {
    pthread_mutex_lock(&sModListMutex);
    int count = 0;
    for (DynosPack *pack = sDynosPacks; pack; pack = pack->next) ++count;
    pthread_mutex_unlock(&sModListMutex);
    return count;
}

const char *mod_runtime_dynos_pack_name(int index) {
    static char result[MOD_MAX_NAME];
    result[0] = 0;
    if (index < 0) return NULL;
    pthread_mutex_lock(&sModListMutex);
    for (DynosPack *pack = sDynosPacks; pack; pack = pack->next) {
        if (!index--) { snprintf(result, sizeof(result), "%s", pack->name); break; }
    }
    pthread_mutex_unlock(&sModListMutex);
    return result[0] ? result : NULL;
}

const char *mod_runtime_dynos_pack_summary(int index) {
    static char result[192];
    result[0] = 0;
    if (index < 0) return NULL;
    pthread_mutex_lock(&sModListMutex);
    for (DynosPack *pack = sDynosPacks; pack; pack = pack->next) {
        if (!index--) {
            snprintf(result, sizeof(result),
                     "%u model bins · %u texture assets · %u collision · %u levels · %u behaviors · %u audio · %u other · inactive",
                     pack->model_bins, pack->texture_assets, pack->collision_assets,
                     pack->level_assets, pack->behavior_assets, pack->audio_assets, pack->other_assets);
            break;
        }
    }
    pthread_mutex_unlock(&sModListMutex);
    return result[0] ? result : NULL;
}

void mod_runtime_shutdown(void) {
    if (!sLua) return;
    call_event(HOOK_ON_EXIT, 0, false, 0);
    for (Mod *mod = sMods; mod; mod = mod->next) unload_mod(mod);
    lua_close(sLua); sLua = NULL;
    for (Mod *mod = sMods; mod;) {
        Mod *next = mod->next; free_string_list(mod->files, mod->file_count);
        free(mod); mod = next;
    }
    sMods = NULL; sHooks = NULL; sActions = NULL; sBehaviors = NULL;
    sNextBehaviorId = 1; sNextObjectToken = 1;
    while (sDynosPacks) { DynosPack *next = sDynosPacks->next; free(sDynosPacks); sDynosPacks = next; }
    while (sObjects) { ModObject *next = sObjects->next; free(sObjects); sObjects = next; }
    pthread_mutex_lock(&sRequestMutex);
    while (sRequestHead) {
        ModRequest *next = sRequestHead->next;
        if (sRequestHead->type == MOD_REQUEST_IMPORT_ZIP && sRequestHead->path) unlink(sRequestHead->path);
        free(sRequestHead->path); free(sRequestHead); sRequestHead = next;
    }
    sRequestTail = NULL;
    pthread_mutex_unlock(&sRequestMutex);
    sInitialized = false; sModsLoaded = false; sHookFailures = 0; sAllocator.bytes = 0;
}
