#include "mod_runtime.h"
#include "mod_audio.h"
#include "dynos_actor.h"
#include "dynos_actor_native.h"

#ifndef EXTERNAL_DATA
#define STB_IMAGE_IMPLEMENTATION
#endif
#include <stb/stb_image.h>

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "audio/external.h"
#include "seq_ids.h"
#include "macros.h"
#include "sm64.h"
#include "level_table.h"
#include "course_table.h"
#include "types.h"
#include "mario_animation_ids.h"
#include "object_fields.h"
#include "engine/behavior_script.h"
#include "engine/graph_node.h"
#include "engine/geo_layout.h"
#include "engine/math_util.h"
#include "behavior_data.h"
#include "mod_behavior_weak.inc"
#include "mod_collision_weak.inc"
#include "game/area.h"
#include "game/camera.h"
#include "game/level_update.h"
#include "game/hud.h"
#include "game/mario.h"
#include "game/mario_step.h"
#include "game/mario_actions_submerged.h"
#include "game/mario_actions_airborne.h"
#include "game/interaction.h"
#include "game/object_helpers.h"
#include "game/mario_misc.h"
#include "game/behavior_actions.h"
#include "game/object_list_processor.h"
#include "game/print.h"
#include "game/spawn_object.h"
#include "game/save_file.h"
#include "game/sound_init.h"
#include "game/memory.h"
#include "game/segment2.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "pc/fs/fs.h"
#include "pc/configfile.h"
#include "pc/mods/mod_resource.h"
#include "pc/gfx/gfx_ios_diagnostics.h"
#include "levels/scripts.h"
#include "macro_preset_types.h"

/* The native engine supplies these callbacks in production. Host-only mod
 * tests may omit the gameplay objects; weak references let those builds
 * report the missing callback through the normal DynOS diagnostic path. */
#pragma weak geo_mirror_mario_set_alpha
#pragma weak geo_switch_mario_eyes
#pragma weak geo_mario_head_rotation
#pragma weak geo_switch_mario_hand
#pragma weak geo_switch_mario_cap_effect
#pragma weak geo_switch_mario_cap_on_off
#pragma weak geo_mario_rotate_wing_cap_wings
#pragma weak geo_switch_mario_hand_grab_pos
#pragma weak geo_mirror_mario_backface_culling
#pragma weak geo_move_mario_part_from_parent

#ifdef VERSION_EU
#include "eu_translation.h"
extern s32 gInGameLanguage;
#endif

#define MOD_MAX_COUNT 128
#define MOD_MAX_NAME 128
#define MOD_MAX_DISPLAY_NAME 192
#define MOD_MAX_CATEGORY 64
#define MOD_MAX_DESCRIPTION 384
#define MOD_MAX_INCOMPATIBLE 256
#define MOD_METADATA_MAX_BYTES 16384
#define MOD_METADATA_MAX_LINES 128
#define MOD_METADATA_MAX_LINE 1024
#define MOD_MAX_FILES 512
#define MOD_MEMORY_LIMIT (32u * 1024u * 1024u)
#define MOD_INSTRUCTION_LIMIT 50000
#define MOD_INSTRUCTION_QUANTUM 1000
#define MOD_EVENT_COUNT 69
#define MOD_DYNAMIC_ID_BASE 0x70000000u
#define MOD_ACTION_INDEX_MASK (ACT_ID_MASK & ~ACT_GROUP_MASK)
#define MOD_ACTIONS_PER_GROUP (MOD_ACTION_INDEX_MASK + 1u)
#define MOD_ACTION_SLOT_COUNT (ACT_ID_MASK + 1u)
#define MOD_STORAGE_MAX_KEYS 4096u
#define MOD_STORAGE_MAX_ENTRY 1024u
#define MOD_STORAGE_MAX_BYTES (1024u * 1024u)
#define MOD_MAX_COMMANDS 128u
#define MOD_MAX_OWNED_OBJECTS 64u
#define MOD_MAX_TRACKED_OBJECTS 128u
#define MOD_MAX_NATIVE_BEHAVIOR_STATES 16384u
#define MOD_MAX_POPUPS 16u
#define MOD_MAX_POPUP_TEXT 256u
#define MOD_MAX_RENDER_HOOK_OWNERS 512u
#define MOD_MAX_SYNC_HOOKS 512u
#define MOD_MAX_CUSTOM_ANIMATIONS 512u
#define MOD_MAX_CUSTOM_ANIMATION_BYTES (4u * 1024u * 1024u)
#define MOD_MAX_CUSTOM_ANIMATION_ARRAY_ELEMENTS 1048576u
#define MOD_MAX_CUSTOM_ANIMATION_GLOBAL_COUNT 4096u
#define MOD_MAX_CUSTOM_ANIMATION_GLOBAL_BYTES (16u * 1024u * 1024u)
#define MOD_MAX_DYNOS_BEHAVIOR_BYTES (256u * 1024u)
#define MOD_MAX_DYNOS_BEHAVIOR_WORDS 4096u
#define MOD_MAX_DYNOS_BEHAVIORS 128u
#define MOD_MAX_DYNOS_BEHAVIOR_TOKENS 64u
#define MOD_MAX_DYNOS_TEXTURE_FILE_BYTES (2u * 1024u * 1024u)
/* Real CoopDX packs include 1024x512 HUD textures (for example DVD Screensaver).
 * Keep the larger decode bounded by the existing per-mod/global byte budgets. */
#define MOD_MAX_DYNOS_TEXTURE_DIMENSION 1024u
#define MOD_MAX_DYNOS_TEXTURES_PER_MOD 256u
#define MOD_MAX_DYNOS_TEXTURE_BYTES_PER_MOD (32u * 1024u * 1024u)
#define MOD_MAX_DYNOS_TEXTURE_BYTES_GLOBAL (64u * 1024u * 1024u)
#define MOD_MAX_DYNOS_TEXTURE_KEYS 2048u
#define MOD_MAX_DYNOS_MODEL_BYTES_PER_MOD (16u * 1024u * 1024u)
#define MOD_MAX_DYNOS_MODEL_BYTES_GLOBAL (64u * 1024u * 1024u)
#define MOD_DYNOS_MODEL_SLOT_FIRST 0xE6
#define MOD_DYNOS_MODEL_SLOT_LAST 0xFF
#define MOD_DYNOS_EXTENDED_MODEL_BASE 0x10000u
#define MOD_NATIVE_BEHAVIOR_COUNT 541
#define MOD_BEHAVIOR_WARNING_WORDS ((MOD_NATIVE_BEHAVIOR_COUNT + 63) / 64)

enum { HOOK_UPDATE = 0, HOOK_MARIO_UPDATE = 1, HOOK_BEFORE_MARIO_UPDATE = 2,
       HOOK_ON_SET_MARIO_ACTION = 3, HOOK_BEFORE_PHYS_STEP = 4,
       HOOK_ALLOW_PVP_ATTACK = 5, HOOK_ON_PVP_ATTACK = 6,
       HOOK_ON_PLAYER_CONNECTED = 7, HOOK_ON_PLAYER_DISCONNECTED = 8,
       HOOK_ON_HUD_RENDER = 9, HOOK_ALLOW_INTERACT = 10, HOOK_ON_INTERACT = 11,
       HOOK_ON_LEVEL_INIT = 12, HOOK_ON_WARP = 13, HOOK_USE_ACT_SELECT = 23, HOOK_ON_DEATH = 21,
       HOOK_ON_SET_CAMERA_MODE = 19, HOOK_ON_CHANGE_CAMERA_ANGLE = 24,
       HOOK_ON_OBJECT_RENDER = 20, HOOK_OBJECT_SET_MODEL = 28,
       HOOK_ON_PACKET_RECEIVE = 22, HOOK_ON_PAUSE_EXIT = 17, HOOK_ALLOW_HAZARD_SURFACE = 26,
       HOOK_ON_EXIT = 34, HOOK_ON_HUD_RENDER_BEHIND = 36, HOOK_MIRROR_MARIO_RENDER = 38 };

typedef struct Mod Mod;
typedef struct ModHook ModHook;
typedef struct ModAction ModAction;
typedef struct ModBehavior ModBehavior;
typedef struct ModNativeBehaviorState ModNativeBehaviorState;
typedef struct ModAnimation ModAnimation;
typedef struct ModObject ModObject;
typedef struct ModModule ModModule;
typedef struct ModRequest ModRequest;
typedef struct ModStorageEntry ModStorageEntry;
typedef struct ModMenuItem ModMenuItem;
typedef struct ModObjectRenderOwner ModObjectRenderOwner;
typedef struct ModCommand ModCommand;
typedef struct ModPopup ModPopup;
typedef struct DynosPack DynosPack;
typedef struct ModCustomActionId ModCustomActionId;
typedef struct ModSyncHook ModSyncHook;
typedef struct ModTexture ModTexture;
typedef struct DynosModel DynosModel;
static float checked_lua_finite_float(lua_State *L, int index, const char *name);

struct ModHook { Mod *owner; char *file; int event; int function_ref; bool enabled; bool invalid_output_reported; ModHook *next_global; ModHook *next_owned; };
struct ModSyncHook { Mod *owner; char *file; int table_ref; int field_ref; int tag_ref; int function_ref; bool enabled; ModSyncHook *next_global; ModSyncHook *next_owned; };
struct ModAction { Mod *owner; char *file; uint32_t id; uint32_t interaction_type; int every_frame_ref; int gravity_ref; bool enabled; ModAction *next_global; ModAction *next_owned; };
struct ModBehavior {
    Mod *owner; char *file; char *name; uint32_t id; int object_list;
    int init_ref; int loop_ref; bool enabled; bool native_hook;
    bool replace_behavior; bool state_warned; bool dynos_behavior;
    uintptr_t *script; size_t script_word_count;
    char **lua_tokens; int *lua_function_refs; size_t lua_token_count;
    ModBehavior *next_global; ModBehavior *next_owned;
};
struct ModObject { struct Object *object; Mod *owner; ModBehavior *behavior; bool initialized; bool enabled; ModObject *next; };
struct ModNativeBehaviorState { struct Object *object; uint32_t token; ModBehavior *behavior; bool initialized; ModNativeBehaviorState *next; };
struct ModModule { char *name; int value_ref; bool loading; ModModule *next; };
struct ModAnimation { Mod *owner; char *name; struct Animation *animation; s16 *values; u16 *index; size_t values_length; size_t index_length; size_t bytes; ModAnimation *next_global; ModAnimation *next_owned; };
struct ModTexture { Mod *owner; char *name; char *file; char *key; uint8_t *rgba16; uint8_t *rgba32; uint32_t width; uint32_t height; int format; int size; size_t bytes; ModTexture *next_global; ModTexture *next_owned; };
struct DynosModel {
    Mod *owner;
    char *name;
    uint32_t extended_id;
    uint8_t native_slot;
    DynosActorBundle bundle;
    DynosNativeActor native;
    struct AllocOnlyPool *graph_pool;
    struct GraphNode *graph;
    DynosModel *next_global;
    DynosModel *next_owned;
};
struct ModRequest { int type; int index; int item_index; int64_t value; bool enabled; char target_name[MOD_MAX_NAME]; char *path; ModRequest *next; };
struct DynosPack {
    char name[MOD_MAX_NAME];
    char path[SYS_MAX_PATH];
    unsigned model_bins, texture_assets, collision_assets, level_assets, behavior_assets, audio_assets, other_assets;
    DynosPack *next;
};
struct ModCustomActionId { Mod *owner; uint32_t id; unsigned slot; ModCustomActionId *next_owned; };
struct ModStorageEntry { char *key; size_t key_length; char *value; size_t value_length; ModStorageEntry *next; };
struct ModMenuItem { char label[256]; int type; lua_Integer value, minimum, maximum; int callback_ref; char *file; };
struct ModCommand { char name[64]; char description[256]; int callback_ref; char *file; ModCommand *next; };
struct ModPopup { Mod *owner; char text[MOD_MAX_POPUP_TEXT]; int lines; double created_at; bool active; bool render_warning_reported; };
struct ModObjectRenderOwner { Mod *owner; struct Object *object; uint32_t token; uint8_t render_id; ModObjectRenderOwner *next; };

struct Mod {
    char name[MOD_MAX_NAME];
    char display_name[MOD_MAX_DISPLAY_NAME];
    char category[MOD_MAX_CATEGORY];
    char description[MOD_MAX_DESCRIPTION];
    char incompatible[MOD_MAX_INCOMPATIBLE];
    char path[SYS_MAX_PATH];
    bool enabled;
    bool loaded;
    bool load_failed;
    int env_ref;
    char **files;
    size_t file_count;
    ModModule *modules;
    ModHook *hooks;
    ModSyncHook *sync_hooks;
    ModAction *actions;
    ModBehavior *behaviors;
    ModAnimation *animations;
    ModTexture *textures;
    DynosModel *models;
    ModCustomActionId *custom_action_ids;
    size_t animation_count;
    size_t animation_bytes;
    size_t dynos_texture_count;
    size_t dynos_texture_bytes;
    size_t dynos_model_bytes;
    ModStorageEntry *storage;
    size_t storage_count;
    size_t storage_bytes;
    bool storage_loaded;
    bool gfx_offset_warned;
    bool storage_failed;
    ModMenuItem *menu_items;
    size_t menu_item_count;
    size_t menu_item_capacity;
    ModCommand *commands;
    bool contains_dynos_assets;
    unsigned dynos_behavior_count;
    bool pointer_warned;
    bool texture_lookup_warned;
    bool hud_interpolation_warned;
    bool hud_font_fallback_warned;
    bool extended_model_warned;
    bool animation_warned;
    bool collision_fix_unavailable_warned;
    bool popup_markup_warned;
    bool chat_popup_warned;
    uint64_t behavior_lookup_warned_ids[MOD_BEHAVIOR_WARNING_WORDS];
    bool behavior_lookup_unknown_warned;
    bool network_send_warned;
    bool network_text_color_warned;
    bool packet_hook_warned;
    bool character_sound_warned;
    bool character_animation_warned;
    bool bubble_api_warned;
    bool multiplayer_settings_warned;
    bool player_interaction_warned;
    bool sync_tables_warned;
    uint64_t star_exit_setting_order;
    bool stay_in_level_after_star;
    uint64_t level_setting_order[MOD_LEVEL_VALUE_COUNT];
    int32_t level_setting_value[MOD_LEVEL_VALUE_COUNT];
    bool level_setting_warned[MOD_LEVEL_VALUE_COUNT];
    Mod *next;
};

typedef struct { size_t bytes; } LuaAllocator;
typedef struct { lua_Integer id; const char *name; const BehaviorScript *script; } NativeBehavior;
typedef struct { lua_Integer id; const char *name; int native_model; } NativeModel;
typedef struct { const char *name; const Collision *data; } NativeCollision;
typedef enum { LUA_UD_MARIO, LUA_UD_CONTROLLER, LUA_UD_VEC_F, LUA_UD_VEC_S,
               LUA_UD_VEC_F_SNAPSHOT, LUA_UD_VEC_S_SNAPSHOT, LUA_UD_MARIO_BODY_STATE,
               LUA_UD_PLAYER_CAMERA_STATE, LUA_UD_OBJECT,
               LUA_UD_OBJECT_HEADER, LUA_UD_OBJECT_GFX, LUA_UD_GRAPH_NODE,
               LUA_UD_GRAPH_NODE_OBJECT, LUA_UD_GFX_OFFSET, LUA_UD_ANIMATION_INFO,
               LUA_UD_ANIMATION, LUA_UD_CHARACTER, LUA_UD_SURFACE,
               LUA_UD_CAMERA, LUA_UD_AREA, LUA_UD_LAKITU_STATE, LUA_UD_POINTER,
               LUA_UD_TEXTURE_INFO } LuaUserdataKind;
typedef struct {
    LuaUserdataKind kind;
    int field;
    uintptr_t pointer;
    struct Object *object;
    uint32_t token;
    struct GraphNode *graph_node;
    struct GraphNodeObject *graph_object;
    float *float_vector;
    int16_t *short_vector;
    float vector_snapshot[3];
    int16_t short_vector_snapshot[3];
    float *shadow_position;
    bool *disable_automatic_shadow_position;
    struct Surface surface;
} LuaUserdata;

enum { VEC_MARIO_POS, VEC_MARIO_VEL, VEC_MARIO_FACE_ANGLE, VEC_MARIO_ANGLE_VEL,
       VEC_GLOBAL_SOUND_SOURCE, VEC_DEFAULT_SOUND_ARGS, VEC_OBJECT_CAMERA_TO_OBJECT };
enum { VEC_DIRECT_F = 100, VEC_DIRECT_S, VEC_OBJECT_SHADOW_POSITION };
enum { HOOK_FN_INIT, HOOK_FN_LOOP, HOOK_FN_ACTION, HOOK_FN_GRAVITY };
enum { MOD_POINTER_BEHAVIOR, MOD_POINTER_COLLISION };

static lua_State *sLua;
static LuaAllocator sAllocator;
static Mod *sMods;
static DynosPack *sDynosPacks;
static Mod *sCurrentMod;
static const char *sCurrentFile;
static Mod *sHudHiddenOwner;
static ModHook *sHooks;
static ModSyncHook *sSyncHooks;
static unsigned sSyncHookCount;
static ModAction *sActions;
static ModBehavior *sBehaviors;
static ModAnimation *sAnimations;
static ModTexture *sTextures;
static DynosModel *sDynosModels;
static DynosModel *sPlayerDynosModel;
static uint64_t sPlayerDynosTraversalFrames;
static uint32_t sNextDynosModelId = MOD_DYNOS_EXTENDED_MODEL_BASE;
static size_t sDynosModelBytes;
static size_t sAnimationCount;
static size_t sAnimationBytes;
static size_t sDynosTextureBytes;
static char sDynosTextureKeys[MOD_MAX_DYNOS_TEXTURE_KEYS][48];
static size_t sDynosTextureKeyCount;
static ModObject *sObjects;
static ModNativeBehaviorState *sNativeBehaviorStates;
static unsigned sNativeBehaviorStateCount;
static ModObjectRenderOwner *sRenderHookOwners;
static unsigned sRenderHookOwnerCount;
static uint32_t sNextBehaviorId = 1;
static uint32_t sNextObjectToken = 1;
/* Native and scripted actors both come from the fixed object pool. A Lua
 * reference must never start referring to a new actor when a slot is reused. */
typedef struct {
    struct Object *object;
    uint32_t token;
    float shadow_position[3];
    bool disable_automatic_shadow_position;
    Mod *visibility_owner;
    bool original_visible;
    bool written_visible;
} ObjectReference;
static ObjectReference sObjectRefs[OBJECT_POOL_CAPACITY];
static unsigned sHookFailures;
static uint64_t sStarExitSettingOrder;
static uint64_t sLevelSettingOrder;
static unsigned sCallbackDepth;
static int sInstructionBudget;
static bool sInitialized;
static bool sModsLoaded;
static bool sActionSlots[MOD_ACTION_SLOT_COUNT];
static ModBehavior *find_behavior(uint32_t id);
static ModAnimation *find_custom_animation(const char *name);
static const BehaviorScript *native_behavior_from_id(lua_Integer id);
static int behavior_id_error(lua_State *L, lua_Integer id, const char *api);
static void remove_native_behavior_states(Mod *owner, struct Object *object);
static ModPopup sPopups[MOD_MAX_POPUPS];
static struct GraphNodeObject *sMirrorMarioGraph;
static float sMirrorMarioShadowPosition[3];
static bool sMirrorMarioDisableAutomaticShadowPosition;
static pthread_mutex_t sRequestMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sModListMutex = PTHREAD_MUTEX_INITIALIZER;
static ModRequest *sRequestHead;
static ModRequest *sRequestTail;
static Mod *mod_runtime_find_index(int index);
static void unref_if_valid(int ref);
static void sync_table_new(lua_State *L, Mod *mod);
static void push_native_character(lua_State *L);

static const NativeBehavior sNativeBehaviors[] = {
#include "mod_behavior_entries.inc"
};
_Static_assert(sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0]) == MOD_NATIVE_BEHAVIOR_COUNT,
               "update MOD_NATIVE_BEHAVIOR_COUNT when the generated behavior table changes");
static const NativeModel sNativeModels[] = {
#include "mod_model_entries.inc"
};
static const NativeCollision sNativeCollisions[] = {
#include "mod_collision_entries.inc"
};

#define STUB_LEVEL(_0, levelenum, _2, _3, _4, _5, _6, _7, _8) [levelenum] = false,
#define DEFINE_LEVEL(_0, levelenum, _2, _3, _4, _5, _6, _7, _8, _9, _10) [levelenum] = true,
static const bool sVanillaLevelIds[LEVEL_COUNT] = {
#include "levels/level_defines.h"
};
#undef STUB_LEVEL
#undef DEFINE_LEVEL

enum { MOD_REQUEST_ENABLE = 1, MOD_REQUEST_RELOAD, MOD_REQUEST_IMPORT_ZIP,
       MOD_REQUEST_MENU_VALUE, MOD_REQUEST_PLAYER_DYNOS_MODEL, MOD_REQUEST_PLAYER_NATIVE_MODEL };
enum { MOD_MENU_BUTTON = 0, MOD_MENU_CHECKBOX = 1, MOD_MENU_SLIDER = 2 };

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
           file_has_suffix(path, ".bin") || file_has_suffix(path, ".bhv");
}

static const char *relative_path(const Mod *mod, const char *fullpath) {
    size_t n = strlen(mod->path);
    if (strncmp(mod->path, fullpath, n) || fullpath[n] != '/') return NULL;
    return fullpath + n + 1;
}

static int compare_strings(const void *a, const void *b);

/* CoopDX mods commonly reference an asset by basename while keeping it in a
 * resource folder such as sound/. Resolve only a unique basename inside the
 * owning mod; never search outside its sandbox or choose nondeterministically. */
static bool mod_resolve_resource_path(Mod *mod, const char *requested, char output[241]) {
    if (!mod || !safe_relative_path(requested)) return false;
    if (strlen(requested) >= 241) return false;
    char full[SYS_MAX_PATH];
    int n = snprintf(full, sizeof(full), "%s/%s", mod->path, requested);
    if (n > 0 && (size_t)n < sizeof(full) && fs_sys_file_exists(full)) {
        memcpy(output, requested, strlen(requested) + 1);
        return true;
    }
    if (strchr(requested, '/')) return false;
    fs_pathlist_t paths = fs_sys_enumerate(mod->path, true);
    if (!paths.paths) return false;
    qsort(paths.paths, (size_t)paths.numpaths, sizeof(char *), compare_strings);
    unsigned matches = 0;
    for (int i = 0; i < paths.numpaths; ++i) {
        const char *relative = relative_path(mod, paths.paths[i]);
        if (!relative) continue;
        const char *basename = strrchr(relative, '/');
        basename = basename ? basename + 1 : relative;
        if (strcmp(basename, requested)) continue;
        if (strlen(relative) >= 241) { matches = 2; break; }
        memcpy(output, relative, strlen(relative) + 1);
        ++matches;
        if (matches > 1) break;
    }
    fs_pathlist_free(&paths);
    return matches == 1;
}

static int compare_strings(const void *a, const void *b) {
    const char *const *sa = a, *const *sb = b;
    return strcmp(*sa, *sb);
}

static bool mod_add_file(Mod *mod, const char *path);

static bool mod_file_walk(void *userdata, const char *path) {
    Mod *mod = (Mod *)userdata;
    if (file_is_dynos_asset(path)) mod->contains_dynos_assets = true;
    if (mod->file_count >= MOD_MAX_FILES) return false;
    if (!file_has_suffix(path, ".lua") && !file_has_suffix(path, ".luac")) return true;
    return mod_add_file(mod, path);
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
    for (size_t i = 0; i < mod->file_count; ++i)
        if (!strcmp(mod->files[i], path)) return true;
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

static void metadata_copy(char *destination, size_t capacity, const char *value) {
    if (!destination || capacity == 0 || !value) return;
    while (*value == ' ' || *value == '\t') ++value;
    size_t length = strlen(value);
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t' ||
                      value[length - 1] == '\r' || value[length - 1] == '\n')) --length;
    if (length >= capacity) length = capacity - 1;
    memcpy(destination, value, length);
    destination[length] = '\0';
}

/* CoopDX reads metadata from the initial comment block of main.lua. Parse it
 * as bounded text before executing any script; never evaluate metadata. */
static void read_mod_metadata(Mod *mod) {
    if (!mod) return;
    snprintf(mod->display_name, sizeof(mod->display_name), "%s", mod->name);
    char path[SYS_MAX_PATH];
    const char *metadata_script = NULL;
    for (size_t i = 0; i < mod->file_count; ++i) {
        const char *relative = relative_path(mod, mod->files[i]);
        if (!relative || strchr(relative, '/')) continue;
        if (!metadata_script || !strcmp(relative, "main.lua")) metadata_script = mod->files[i];
        if (!strcmp(relative, "main.lua")) break;
    }
    if (!metadata_script) return;
    int written = snprintf(path, sizeof(path), "%s", metadata_script);
    if (written <= 0 || (size_t)written >= sizeof(path)) return;
    FILE *file = fopen(path, "rb");
    if (!file) return;
    size_t bytes = 0;
    unsigned lines = 0;
    bool found_name = false;
    char line[MOD_METADATA_MAX_LINE];
    while (bytes < MOD_METADATA_MAX_BYTES && lines < MOD_METADATA_MAX_LINES) {
        size_t used = 0;
        int ch;
        while (used + 1 < sizeof(line) && bytes < MOD_METADATA_MAX_BYTES &&
               (ch = fgetc(file)) != EOF) {
            ++bytes;
            if (ch == '\n') break;
            if (ch == '\0') { used = 0; break; }
            line[used++] = (char)ch;
        }
        if (ch == EOF && used == 0) break;
        line[used] = '\0';
        ++lines;
        if (line[0] != '-' || line[1] != '-') break;
        const char *value = NULL;
        char *colon = strchr(line + 2, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *key = line + 2;
        while (*key == ' ' || *key == '\t') ++key;
        value = colon + 1;
        if (!strcmp(key, "name") && !found_name) {
            metadata_copy(mod->display_name, sizeof(mod->display_name), value);
            found_name = true;
        } else if (!strcmp(key, "category") && !mod->category[0])
            metadata_copy(mod->category, sizeof(mod->category), value);
        else if (!strcmp(key, "description") && !mod->description[0])
            metadata_copy(mod->description, sizeof(mod->description), value);
        else if (!strcmp(key, "incompatible") && !mod->incompatible[0])
            metadata_copy(mod->incompatible, sizeof(mod->incompatible), value);
    }
    fclose(file);
}

static bool incompatible_token_lists_match(const char *a, const char *b) {
    if (!a || !b || !*a || !*b) return false;
    const char *at = a;
    while (*at) {
        while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') ++at;
        const char *a_end = at;
        while (*a_end && *a_end != ' ' && *a_end != '\t' && *a_end != '\r' && *a_end != '\n') ++a_end;
        if (a_end == at) break;
        const char *bt = b;
        while (*bt) {
            while (*bt == ' ' || *bt == '\t' || *bt == '\r' || *bt == '\n') ++bt;
            const char *b_end = bt;
            while (*b_end && *b_end != ' ' && *b_end != '\t' && *b_end != '\r' && *b_end != '\n') ++b_end;
            if (b_end == bt) break;
            if ((size_t)(a_end - at) == (size_t)(b_end - bt) && !memcmp(at, bt, (size_t)(a_end - at)))
                return true;
            bt = b_end;
        }
        at = a_end;
    }
    return false;
}

static Mod *find_enabled_conflict(const Mod *candidate) {
    if (!candidate) return NULL;
    for (Mod *other = sMods; other; other = other->next) {
        if (other == candidate || !other->enabled) continue;
        if (incompatible_token_lists_match(candidate->incompatible, other->incompatible)) return other;
    }
    return NULL;
}

static void resolve_startup_mod_conflicts(void) {
    for (Mod *mod = sMods; mod; mod = mod->next) {
        if (!mod->enabled) continue;
        for (Mod *earlier = sMods; earlier && earlier != mod; earlier = earlier->next) {
            if (!earlier->enabled) continue;
            if (!incompatible_token_lists_match(mod->incompatible, earlier->incompatible)) continue;
            mod->enabled = false;
            mod_logf("warning", mod->name,
                     "disabled during deterministic startup: incompatible with earlier mod '%s'", earlier->name);
            break;
        }
    }
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
    for (Mod *mod = sMods; mod; mod = mod->next) read_mod_metadata(mod);

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

typedef struct { const uint8_t *bytes; size_t size; size_t offset; } DynosBehaviorReader;

static bool dynos_read_u8(DynosBehaviorReader *reader, uint8_t *value) {
    if (!reader || !value || reader->offset >= reader->size) return false;
    *value = reader->bytes[reader->offset++];
    return true;
}

static bool dynos_read_u32(DynosBehaviorReader *reader, uint32_t *value) {
    if (!reader || !value || reader->size - reader->offset < 4) return false;
    const uint8_t *p = reader->bytes + reader->offset;
    *value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    reader->offset += 4;
    return true;
}

static char *dynos_read_string(DynosBehaviorReader *reader, size_t max_length) {
    uint8_t length;
    if (!dynos_read_u8(reader, &length) || length == 0 || length > max_length ||
        reader->size - reader->offset < length ||
        memchr(reader->bytes + reader->offset, '\0', length) != NULL) return NULL;
    char *value = malloc((size_t)length + 1);
    if (!value) return NULL;
    memcpy(value, reader->bytes + reader->offset, length);
    value[length] = '\0';
    reader->offset += length;
    return value;
}

static void free_string_array(char **strings, size_t count) {
    if (!strings) return;
    for (size_t i = 0; i < count; ++i) free(strings[i]);
    free(strings);
}

/* This initial DynOS behavior path deliberately accepts only commands whose
 * semantics and storage are represented by this engine: BEGIN, OR_INT,
 * BEGIN_LOOP, END_LOOP, ID, and CALL_NATIVE_EXT. Other behavior opcodes may
 * contain relocated native pointers or depend on unimplemented DynOS assets. */
static bool dynos_behavior_commands_supported(const uintptr_t *script, size_t words,
                                              size_t token_count, char *reason, size_t reason_size) {
    unsigned loop_depth = 0;
    for (size_t i = 0; i < words;) {
        unsigned opcode = (unsigned)(script[i] >> 24);
        switch (opcode) {
            case 0x00: /* BEGIN */
                if (i != 0) goto unsupported;
                ++i;
                break;
            case 0x08: ++loop_depth; ++i; break;
            case 0x09:
                if (!loop_depth) {
                    snprintf(reason, reason_size, "END_LOOP has no matching BEGIN_LOOP");
                    return false;
                }
                --loop_depth;
                ++i;
                break;
            case 0x11: /* OR_INT */
            case 0x39: /* ID */
                ++i;
                break;
            case 0x3c: { /* CALL_NATIVE_EXT */
                if (i + 1 >= words) {
                    snprintf(reason, reason_size, "CALL_NATIVE_EXT is missing its token operand");
                    return false;
                }
                uint32_t token = (uint32_t)script[i + 1];
                if (token == 0 || token > token_count) {
                    snprintf(reason, reason_size, "CALL_NATIVE_EXT token %u is outside the behavior token table", token);
                    return false;
                }
                i += 2;
                break;
            }
            default:
unsupported:
                snprintf(reason, reason_size, "behavior command 0x%02x is not implemented", opcode);
                return false;
        }
    }
    if (loop_depth) {
        snprintf(reason, reason_size, "behavior script has %u unterminated loop(s)", loop_depth);
        return false;
    }
    return true;
}

static bool dynos_behavior_name_exists(const Mod *mod, const char *name) {
    for (const ModBehavior *behavior = mod->behaviors; behavior; behavior = behavior->next_owned)
        if (behavior->dynos_behavior && behavior->name && !strcmp(behavior->name, name)) return true;
    return false;
}

static bool load_dynos_behavior_file(Mod *mod, const char *relative) {
    size_t file_size = 0;
    uint8_t *bytes = mod_resource_read(mod->path, relative, MOD_MAX_DYNOS_BEHAVIOR_BYTES, &file_size);
    if (!bytes) {
        mod_logf("warning", mod->name, "DynOS behavior %s could not be read safely or exceeds %u bytes",
                 relative, MOD_MAX_DYNOS_BEHAVIOR_BYTES);
        return false;
    }
    DynosBehaviorReader reader = { bytes, file_size, 0 };
    uint8_t type = 0, major = 0, minor = 0, patch = 0;
    char *name = NULL;
    uintptr_t *script = NULL;
    char **tokens = NULL;
    size_t token_count = 0;
    uint32_t word_count = 0;
    bool valid = false;
    char reason[192] = "truncated or malformed behavior resource";
    if (!dynos_read_u8(&reader, &type) || type != 0x16 ||
        !(name = dynos_read_string(&reader, 240)) ||
        !dynos_read_u8(&reader, &major) || !dynos_read_u8(&reader, &minor) ||
        !dynos_read_u8(&reader, &patch) || major != 1 ||
        !dynos_read_u32(&reader, &word_count)) {
        snprintf(reason, sizeof(reason), "unsupported type, name, version, or truncated header");
        goto done;
    }
    (void)minor; (void)patch;
    if (word_count == 0 || word_count > MOD_MAX_DYNOS_BEHAVIOR_WORDS ||
        dynos_behavior_name_exists(mod, name)) {
        snprintf(reason, sizeof(reason), "invalid word count or duplicate behavior name '%s'", name);
        goto done;
    }
    script = calloc(word_count, sizeof(*script));
    tokens = calloc(MOD_MAX_DYNOS_BEHAVIOR_TOKENS, sizeof(*tokens));
    if (!script || !tokens) {
        snprintf(reason, sizeof(reason), "out of memory allocating behavior script");
        goto done;
    }
    for (uint32_t i = 0; i < word_count; ++i) {
        uint32_t word = 0;
        if (!dynos_read_u32(&reader, &word)) goto done;
        if (word == UINT32_C(0x5641554c)) { /* DynOS LUA_VAR_CODE: "LUAV" */
            char *token_name = dynos_read_string(&reader, 240);
            if (!token_name) goto done;
            size_t token = 0;
            while (token < token_count && strcmp(tokens[token], token_name)) ++token;
            if (token == token_count) {
                if (token_count >= MOD_MAX_DYNOS_BEHAVIOR_TOKENS) {
                    free(token_name);
                    snprintf(reason, sizeof(reason), "behavior exceeds %u Lua function tokens", MOD_MAX_DYNOS_BEHAVIOR_TOKENS);
                    goto done;
                }
                tokens[token_count++] = token_name;
            } else {
                free(token_name);
            }
            word = (uint32_t)(token + 1);
        }
        script[i] = (uintptr_t)word;
    }
    if (reader.offset != reader.size) {
        snprintf(reason, sizeof(reason), "trailing bytes after behavior command stream");
        goto done;
    }
    unsigned object_list = (unsigned)((script[0] >> 16) & 0xffu);
    if ((script[0] >> 24) != 0 || object_list >= NUM_OBJ_LISTS || object_list == OBJ_LIST_PLAYER) {
        snprintf(reason, sizeof(reason), "BEGIN command has an invalid object list");
        goto done;
    }
    if (!dynos_behavior_commands_supported(script, word_count, token_count, reason, sizeof(reason))) goto done;
    if (sNextBehaviorId >= 0x10000000u || mod->dynos_behavior_count >= MOD_MAX_DYNOS_BEHAVIORS) {
        snprintf(reason, sizeof(reason), "dynamic behavior registration limit reached");
        goto done;
    }

    ModBehavior *behavior = calloc(1, sizeof(*behavior));
    if (!behavior) {
        snprintf(reason, sizeof(reason), "out of memory registering behavior");
        goto done;
    }
    behavior->file = strdup(relative);
    behavior->name = name; name = NULL;
    behavior->script = script; script = NULL;
    behavior->script_word_count = word_count;
    behavior->lua_tokens = tokens; tokens = NULL;
    behavior->lua_function_refs = malloc(token_count * sizeof(*behavior->lua_function_refs));
    behavior->lua_token_count = token_count;
    if (token_count && behavior->lua_function_refs) {
        for (size_t i = 0; i < token_count; ++i) behavior->lua_function_refs[i] = LUA_NOREF;
    }
    if (!behavior->file || (token_count && !behavior->lua_function_refs)) {
        free(behavior->file); free(behavior->name); free(behavior->script);
        free_string_array(behavior->lua_tokens, behavior->lua_token_count);
        free(behavior->lua_function_refs); free(behavior);
        snprintf(reason, sizeof(reason), "out of memory registering behavior metadata");
        goto done;
    }
    behavior->owner = mod;
    behavior->id = MOD_DYNAMIC_ID_BASE | sNextBehaviorId++;
    behavior->object_list = (int)object_list;
    behavior->enabled = true;
    behavior->dynos_behavior = true;
    behavior->init_ref = behavior->loop_ref = LUA_NOREF;
    behavior->next_global = sBehaviors; sBehaviors = behavior;
    behavior->next_owned = mod->behaviors; mod->behaviors = behavior;
    ++mod->dynos_behavior_count;
    mod_logf("info", mod->name,
             "activated DynOS behavior %s as local ID %u (%u commands, %zu Lua token(s))",
             behavior->name, behavior->id, word_count, token_count);
    valid = true;
done:
    if (!valid) mod_logf("warning", mod->name, "DynOS behavior %s was not activated: %s", relative, reason);
    free(bytes);
    free(name);
    free(script);
    free_string_array(tokens, token_count);
    return valid;
}

static char *dynos_texture_key_intern(const uint8_t *bytes, size_t size) {
    uint64_t hash_a = UINT64_C(1469598103934665603);
    uint64_t hash_b = UINT64_C(7809847782465536322);
    for (size_t i = 0; i < size; ++i) {
        hash_a = (hash_a ^ bytes[i]) * UINT64_C(1099511628211);
        hash_b ^= (uint64_t)bytes[i] + UINT64_C(0x9e3779b97f4a7c15) + (hash_b << 6) + (hash_b >> 2);
    }
    char key[48];
    snprintf(key, sizeof(key), "m69tex:%016" PRIx64 "%016" PRIx64, hash_a, hash_b);
    for (size_t i = 0; i < sDynosTextureKeyCount; ++i)
        if (!strcmp(sDynosTextureKeys[i], key)) return sDynosTextureKeys[i];
    if (sDynosTextureKeyCount >= MOD_MAX_DYNOS_TEXTURE_KEYS) return NULL;
    snprintf(sDynosTextureKeys[sDynosTextureKeyCount], sizeof(sDynosTextureKeys[0]), "%s", key);
    return sDynosTextureKeys[sDynosTextureKeyCount++];
}

/* Actor-bundle textures share the same renderer key and per-mod ownership as
 * standalone .tex files. The caller rolls back newly added textures if a
 * model fails to link or register. */
static const void *register_dynos_actor_texture(void *context, const char *name,
                                                 const uint8_t *encoded, size_t encoded_size) {
    Mod *mod = context;
    if (!mod || !name || !*name || !encoded || encoded_size < 8 ||
        encoded_size > MOD_MAX_DYNOS_TEXTURE_FILE_BYTES ||
        memcmp(encoded, "\x89PNG\r\n\x1a\n", 8) ||
        mod->dynos_texture_count >= MOD_MAX_DYNOS_TEXTURES_PER_MOD) return NULL;
    int width, height, channels;
    if (!stbi_info_from_memory(encoded, (int)encoded_size, &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > MOD_MAX_DYNOS_TEXTURE_DIMENSION ||
        height > MOD_MAX_DYNOS_TEXTURE_DIMENSION) return NULL;
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / 6u) return NULL;
    size_t bytes = pixels * 6u;
    if (bytes > MOD_MAX_DYNOS_TEXTURE_BYTES_PER_MOD ||
        mod->dynos_texture_bytes > MOD_MAX_DYNOS_TEXTURE_BYTES_PER_MOD - bytes ||
        bytes > MOD_MAX_DYNOS_TEXTURE_BYTES_GLOBAL ||
        sDynosTextureBytes > MOD_MAX_DYNOS_TEXTURE_BYTES_GLOBAL - bytes) return NULL;
    uint8_t *rgba = stbi_load_from_memory(encoded, (int)encoded_size, &width, &height, NULL, 4);
    if (!rgba) return NULL;
    if (width <= 0 || height <= 0 || width > MOD_MAX_DYNOS_TEXTURE_DIMENSION ||
        height > MOD_MAX_DYNOS_TEXTURE_DIMENSION || (size_t)width * height != pixels) {
        stbi_image_free(rgba);
        return NULL;
    }
    uint8_t *rgba16 = malloc(pixels * 2u);
    ModTexture *texture = calloc(1, sizeof(*texture));
    char *key = dynos_texture_key_intern(encoded, encoded_size);
    if (!rgba16 || !texture || !key || !(texture->name = strdup(name)) ||
        !(texture->file = strdup("embedded DynOS actor"))) {
        free(rgba16);
        if (texture) { free(texture->name); free(texture->file); free(texture); }
        stbi_image_free(rgba);
        return NULL;
    }
    for (size_t i = 0; i < pixels; ++i) {
        uint16_t color = (uint16_t)(((rgba[i * 4] >> 3) << 11) |
                                    ((rgba[i * 4 + 1] >> 3) << 6) |
                                    ((rgba[i * 4 + 2] >> 3) << 1) |
                                    (rgba[i * 4 + 3] >= 128));
        rgba16[i * 2] = (uint8_t)(color >> 8);
        rgba16[i * 2 + 1] = (uint8_t)color;
    }
    texture->owner = mod;
    texture->key = key;
    texture->rgba16 = rgba16;
    texture->rgba32 = rgba;
    texture->width = (uint32_t)width;
    texture->height = (uint32_t)height;
    texture->format = G_IM_FMT_RGBA;
#ifdef EXTERNAL_DATA
    texture->size = G_IM_SIZ_32b;
#else
    texture->size = G_IM_SIZ_16b;
#endif
    texture->bytes = bytes;
    texture->next_global = sTextures;
    sTextures = texture;
    texture->next_owned = mod->textures;
    mod->textures = texture;
    ++mod->dynos_texture_count;
    mod->dynos_texture_bytes += bytes;
    sDynosTextureBytes += bytes;
    return key;
}

/* DynOS texture files contain DATA_TYPE_TEXTURE, a length-prefixed symbol,
 * a little-endian PNG byte count, and a PNG payload. Return 0 for a different
 * symbol, 1 for an activated texture, and -1 for a matching malformed asset. */
static int try_load_dynos_texture(Mod *mod, const char *relative, const char *requested_name,
                                  char *reason, size_t reason_size) {
    size_t file_size = 0;
    uint8_t *bytes = mod_resource_read(mod->path, relative,
                                       MOD_MAX_DYNOS_TEXTURE_FILE_BYTES, &file_size);
    if (!bytes) return 0;
    DynosBehaviorReader reader = { bytes, file_size, 0 };
    uint8_t type = 0;
    char *name = NULL;
    uint32_t encoded_size = 0;
    int result = 0;
    if (!dynos_read_u8(&reader, &type) || type != 0x02 ||
        !(name = dynos_read_string(&reader, 240)) ||
        !dynos_read_u32(&reader, &encoded_size)) goto done;
    if (strcmp(name, requested_name)) goto done;
    result = -1;
    if (encoded_size == 0 || encoded_size != reader.size - reader.offset ||
        encoded_size > INT_MAX || encoded_size < 8 ||
        memcmp(reader.bytes + reader.offset, "\x89PNG\r\n\x1a\n", 8) != 0) {
        snprintf(reason, reason_size, "invalid PNG payload length or signature");
        goto done;
    }
    if (mod->dynos_texture_count >= MOD_MAX_DYNOS_TEXTURES_PER_MOD) {
        snprintf(reason, reason_size, "mod texture limit reached (%u)", MOD_MAX_DYNOS_TEXTURES_PER_MOD);
        goto done;
    }
    int width = 0, height = 0, channels = 0;
    const uint8_t *encoded = reader.bytes + reader.offset;
    if (!stbi_info_from_memory(encoded, (int)encoded_size, &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > MOD_MAX_DYNOS_TEXTURE_DIMENSION ||
        height > MOD_MAX_DYNOS_TEXTURE_DIMENSION) {
        snprintf(reason, reason_size, "PNG dimensions are invalid or exceed %u x %u",
                 MOD_MAX_DYNOS_TEXTURE_DIMENSION, MOD_MAX_DYNOS_TEXTURE_DIMENSION);
        goto done;
    }
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > (SIZE_MAX / 6u) || pixels * 6u > MOD_MAX_DYNOS_TEXTURE_BYTES_PER_MOD ||
        mod->dynos_texture_bytes > MOD_MAX_DYNOS_TEXTURE_BYTES_PER_MOD - pixels * 6u ||
        sDynosTextureBytes > MOD_MAX_DYNOS_TEXTURE_BYTES_GLOBAL - pixels * 6u) {
        snprintf(reason, reason_size, "decoded texture memory budget exceeded");
        goto done;
    }
    uint8_t *rgba = stbi_load_from_memory(encoded, (int)encoded_size, &width, &height, NULL, 4);
    if (!rgba) {
        snprintf(reason, reason_size, "PNG decoder rejected the texture payload");
        goto done;
    }
    if (width <= 0 || height <= 0 || width > MOD_MAX_DYNOS_TEXTURE_DIMENSION ||
        height > MOD_MAX_DYNOS_TEXTURE_DIMENSION || (size_t)width * (size_t)height != pixels) {
        stbi_image_free(rgba);
        snprintf(reason, reason_size, "PNG dimensions changed during decode");
        goto done;
    }
    uint8_t *rgba16 = malloc(pixels * 2u);
    if (!rgba16) {
        stbi_image_free(rgba);
        snprintf(reason, reason_size, "out of memory decoding texture");
        goto done;
    }
    for (size_t i = 0; i < pixels; ++i) {
        uint16_t color = (uint16_t)(((rgba[i * 4] >> 3) << 11) |
                                    ((rgba[i * 4 + 1] >> 3) << 6) |
                                    ((rgba[i * 4 + 2] >> 3) << 1) |
                                    (rgba[i * 4 + 3] >= 128));
        rgba16[i * 2] = (uint8_t)(color >> 8);
        rgba16[i * 2 + 1] = (uint8_t)color;
    }
    char *key = dynos_texture_key_intern(encoded, encoded_size);
    ModTexture *texture = calloc(1, sizeof(*texture));
    if (!key || !texture || !(texture->file = strdup(relative))) {
        if (texture) free(texture->file);
        free(texture);
        free(rgba16);
        stbi_image_free(rgba);
        snprintf(reason, reason_size, key ? "out of memory registering texture" : "texture identity limit reached");
        goto done;
    }
    texture->owner = mod;
    texture->name = name;
    name = NULL;
    texture->key = key;
    texture->rgba16 = rgba16;
    texture->rgba32 = rgba;
    texture->width = (uint32_t)width;
    texture->height = (uint32_t)height;
#ifdef EXTERNAL_DATA
    /* CoopDX expands PNG-backed DynOS textures to RGBA32 before exposing
     * TextureInfo. The Metal upload path consumes the same RGBA8 bytes. */
    texture->format = G_IM_FMT_RGBA;
    texture->size = G_IM_SIZ_32b;
#else
    texture->format = G_IM_FMT_RGBA;
    texture->size = G_IM_SIZ_16b;
#endif
    texture->bytes = pixels * 6u;
    texture->next_global = sTextures;
    sTextures = texture;
    texture->next_owned = mod->textures;
    mod->textures = texture;
    ++mod->dynos_texture_count;
    mod->dynos_texture_bytes += texture->bytes;
    sDynosTextureBytes += texture->bytes;
#ifdef EXTERNAL_DATA
    mod_logf("info", mod->name, "decoded DynOS texture %s (%ux%u RGBA32, %zu decoded bytes)",
             texture->name, texture->width, texture->height, texture->bytes);
#else
    mod_logf("info", mod->name, "decoded DynOS texture %s (%ux%u RGBA16, %zu decoded bytes)",
             texture->name, texture->width, texture->height, texture->bytes);
#endif
    result = 1;
done:
    if (result < 0)
        mod_logf("warning", mod->name, "DynOS texture %s was not activated: %s", relative, reason);
    free(name);
    free(bytes);
    return result;
}

static ModTexture *find_mod_texture(Mod *mod, const char *name) {
    for (ModTexture *texture = mod ? mod->textures : NULL; texture; texture = texture->next_owned)
        if (!strcmp(texture->name, name)) return texture;
    if (!mod || !safe_relative_path(name)) return NULL;
    fs_pathlist_t paths = fs_sys_enumerate(mod->path, true);
    if (!paths.paths) return NULL;
    qsort(paths.paths, (size_t)paths.numpaths, sizeof(char *), compare_strings);
    ModTexture *found = NULL;
    for (int i = 0; i < paths.numpaths; ++i) {
        if (!file_has_suffix(paths.paths[i], ".tex")) continue;
        const char *relative = relative_path(mod, paths.paths[i]);
        if (!relative) continue;
        char reason[160] = "unsupported or malformed texture resource";
        int loaded = try_load_dynos_texture(mod, relative, name, reason, sizeof(reason));
        (void)reason;
        if (loaded > 0) { found = mod->textures; break; }
        for (ModTexture *texture = mod->textures; texture; texture = texture->next_owned)
            if (!strcmp(texture->name, name)) { found = texture; break; }
        if (found) break;
    }
    fs_pathlist_free(&paths);
    return found;
}

bool mod_runtime_dynamic_texture_lookup(const char *key, const uint8_t **rgba,
                                        uint32_t *width, uint32_t *height) {
    if (!key || !rgba || !width || !height || strncmp(key, "m69tex:", 7)) return false;
    for (ModTexture *texture = sTextures; texture; texture = texture->next_global) {
        if (texture->owner && texture->owner->enabled && texture->owner->loaded &&
            !strcmp(texture->key, key)) {
            *rgba = texture->rgba32;
            *width = texture->width;
            *height = texture->height;
            return true;
        }
    }
    return false;
}

static void rollback_dynos_actor_textures(Mod *mod, ModTexture *previous_head) {
    while (mod->textures != previous_head) {
        ModTexture *texture = mod->textures;
        mod->textures = texture->next_owned;
        ModTexture **link = &sTextures;
        while (*link && *link != texture) link = &(*link)->next_global;
        if (*link == texture) *link = texture->next_global;
        if (mod->dynos_texture_count) --mod->dynos_texture_count;
        if (mod->dynos_texture_bytes >= texture->bytes) mod->dynos_texture_bytes -= texture->bytes;
        if (sDynosTextureBytes >= texture->bytes) sDynosTextureBytes -= texture->bytes;
        stbi_image_free(texture->rgba32);
        free(texture->rgba16);
        free(texture->name);
        free(texture->file);
        free(texture);
    }
}

/* Mario GeoLayout callbacks use Mat4 * in their declarations while the scene
 * graph ABI carries an untyped context pointer. Adapt at the ABI boundary so
 * sanitizer builds and native graph dispatch use the exact GraphNodeFunc
 * signature without casting incompatible function pointers. */
#define DYNOS_MARIO_GEO_ADAPTER(adapter, target) \
    static Gfx *adapter(s32 call, struct GraphNode *node, void *context) { \
        return target(call, node, (Mat4 *)context); \
    }
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_mirror_mario_set_alpha, geo_mirror_mario_set_alpha)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_switch_mario_eyes, geo_switch_mario_eyes)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_mario_head_rotation, geo_mario_head_rotation)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_switch_mario_hand, geo_switch_mario_hand)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_switch_mario_cap_effect, geo_switch_mario_cap_effect)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_switch_mario_cap_on_off, geo_switch_mario_cap_on_off)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_mario_rotate_wing_cap_wings, geo_mario_rotate_wing_cap_wings)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_switch_mario_hand_grab_pos, geo_switch_mario_hand_grab_pos)
DYNOS_MARIO_GEO_ADAPTER(dynos_geo_mirror_mario_backface_culling, geo_mirror_mario_backface_culling)
static Gfx *dynos_geo_move_mario_part_from_parent(s32 call, struct GraphNode *node,
                                                   void *context) {
    return geo_move_mario_part_from_parent(call, node, *(Mat4 *)context);
}
#undef DYNOS_MARIO_GEO_ADAPTER

static const void *dynos_actor_geo_function(void *context, uint32_t index) {
    (void)context;
    switch (index) {
        /* Indexes from upstream's ordered sDynosBuiltinFuncs table. These
         * callbacks are implemented by the native engine and used by OMM's
         * original Peach actor GeoLayouts. Do not compact/reorder this map. */
        case 0: return geo_mirror_mario_set_alpha ? (const void *)dynos_geo_mirror_mario_set_alpha : NULL;
        case 2: return geo_switch_mario_eyes ? (const void *)dynos_geo_switch_mario_eyes : NULL;
        case 4: return geo_mario_head_rotation ? (const void *)dynos_geo_mario_head_rotation : NULL;
        case 5: return geo_switch_mario_hand ? (const void *)dynos_geo_switch_mario_hand : NULL;
        case 7: return geo_switch_mario_cap_effect ? (const void *)dynos_geo_switch_mario_cap_effect : NULL;
        case 8: return geo_switch_mario_cap_on_off ? (const void *)dynos_geo_switch_mario_cap_on_off : NULL;
        case 9: return geo_mario_rotate_wing_cap_wings ? (const void *)dynos_geo_mario_rotate_wing_cap_wings : NULL;
        case 10: return geo_switch_mario_hand_grab_pos ? (const void *)dynos_geo_switch_mario_hand_grab_pos : NULL;
        case 12: return geo_mirror_mario_backface_culling ? (const void *)dynos_geo_mirror_mario_backface_culling : NULL;
        case 14: return (const void *)geo_update_layer_transparency;
        case 15: return (const void *)geo_switch_anim_state;
        case 37: return geo_move_mario_part_from_parent ? (const void *)dynos_geo_move_mario_part_from_parent : NULL;
        default: return NULL;
    }
}

static DynosModel *find_dynos_model(Mod *mod, const char *name) {
    for (DynosModel *model = mod ? mod->models : NULL; model; model = model->next_owned)
        if (!strcmp(model->name, name)) return model;
    return NULL;
}

static DynosModel *load_dynos_model(Mod *mod, const char *name,
                                    char *reason, size_t reason_size) {
    DynosModel *existing = find_dynos_model(mod, name);
    if (existing) return existing;
    if (!mod || !safe_relative_path(name) || strchr(name, '/') ||
        sNextDynosModelId == UINT32_MAX) {
        snprintf(reason, reason_size, "invalid model name or exhausted model IDs");
        return NULL;
    }
    unsigned slot = 0;
    for (unsigned candidate = MOD_DYNOS_MODEL_SLOT_FIRST;
         candidate <= MOD_DYNOS_MODEL_SLOT_LAST; ++candidate) {
        bool used = false;
        for (DynosModel *model = sDynosModels; model; model = model->next_global)
            used |= model->native_slot == candidate;
        if (!used && gLoadedGraphNodes && !gLoadedGraphNodes[candidate]) {
            slot = candidate;
            break;
        }
    }
    if (!slot) {
        snprintf(reason, reason_size, "no free native model slots (0xE6–0xFF)");
        return NULL;
    }
    char relative[320];
    if (snprintf(relative, sizeof(relative), "actors/%s.bin", name) >= (int)sizeof(relative)) {
        snprintf(reason, reason_size, "model path exceeds limit");
        return NULL;
    }
    size_t file_size = 0;
    uint8_t *file = mod_resource_read(mod->path, relative, 8u * 1024u * 1024u, &file_size);
    if (!file) {
        snprintf(reason, reason_size, "actor bundle was not found or was blocked by sandbox validation");
        return NULL;
    }
    DynosModel *model = calloc(1, sizeof(*model));
    ModTexture *previous_textures = mod->textures;
    if (!model) snprintf(reason, reason_size, "out of memory allocating model");
    bool ready = model && dynos_actor_decode(file, file_size, &model->bundle, reason, reason_size);
    if (ready && (model->bundle.size > MOD_MAX_DYNOS_MODEL_BYTES_PER_MOD ||
                  mod->dynos_model_bytes > MOD_MAX_DYNOS_MODEL_BYTES_PER_MOD - model->bundle.size ||
                  model->bundle.size > MOD_MAX_DYNOS_MODEL_BYTES_GLOBAL ||
                  sDynosModelBytes > MOD_MAX_DYNOS_MODEL_BYTES_GLOBAL - model->bundle.size)) {
        snprintf(reason, reason_size, "DynOS actor memory budget exceeded");
        ready = false;
    }
    ready = ready &&
        dynos_actor_native_build(&model->bundle, register_dynos_actor_texture,
                                 dynos_actor_geo_function, mod, &model->native,
                                 reason, reason_size) &&
        dynos_actor_native_validate_root_geo(&model->bundle, &model->native,
                                             name, reason, reason_size);
    free(file);
    if (ready) {
        const GeoLayout *geo = dynos_actor_native_find(&model->bundle, &model->native, 5, name);
        const size_t graph_bytes = 64u * 1024u;
        model->graph_pool = malloc(sizeof(*model->graph_pool) + graph_bytes);
        if (model->graph_pool) {
            model->graph_pool->totalSpace = (s32)graph_bytes;
            model->graph_pool->usedSpace = 0;
            model->graph_pool->startPtr = (u8 *)(model->graph_pool + 1);
            model->graph_pool->freePtr = model->graph_pool->startPtr;
            model->graph = process_geo_layout(model->graph_pool, (void *)geo);
        }
        ready = model->graph != NULL;
        if (!ready) snprintf(reason, reason_size, "native geolayout graph allocation failed");
    }
    if (!ready) {
        rollback_dynos_actor_textures(mod, previous_textures);
        if (model) {
            free(model->graph_pool);
            dynos_actor_native_free(&model->native);
            dynos_actor_free(&model->bundle);
            free(model);
        }
        return NULL;
    }
    model->name = strdup(name);
    if (!model->name) {
        rollback_dynos_actor_textures(mod, previous_textures);
        free(model->graph_pool);
        dynos_actor_native_free(&model->native);
        dynos_actor_free(&model->bundle);
        free(model);
        snprintf(reason, reason_size, "out of memory naming model");
        return NULL;
    }
    model->owner = mod;
    mod->dynos_model_bytes += model->bundle.size;
    sDynosModelBytes += model->bundle.size;
    model->native_slot = (uint8_t)slot;
    model->extended_id = sNextDynosModelId++;
    gLoadedGraphNodes[slot] = model->graph;
    model->next_global = sDynosModels;
    sDynosModels = model;
    model->next_owned = mod->models;
    mod->models = model;
    mod_logf("info", mod->name,
             "activated DynOS actor %s as extended model %u (native slot 0x%02x, %zu records)",
             name, model->extended_id, slot, model->bundle.record_count);
    return model;
}

#ifdef MOD_RUNTIME_HOST_TEST
bool mod_runtime_host_load_dynos_model(size_t mod_index, const char *name,
                                       int *extended_id, char *error,
                                       size_t error_size) {
    Mod *mod = sMods;
    while (mod && mod_index) { mod = mod->next; --mod_index; }
    if (!mod || !name || !sLua) return false;
    Mod *previous = sCurrentMod;
    sCurrentMod = mod;
    DynosModel *model = load_dynos_model(mod, name, error, error_size);
    sCurrentMod = previous;
    if (!model) return false;
    if (extended_id) *extended_id = (int)model->extended_id;
    return true;
}
#endif

static void load_dynos_behaviors(Mod *mod) {
    fs_pathlist_t paths = fs_sys_enumerate(mod->path, true);
    if (!paths.paths) return;
    qsort(paths.paths, (size_t)paths.numpaths, sizeof(char *), compare_strings);
    unsigned loaded = 0;
    for (int i = 0; i < paths.numpaths; ++i) {
        if (!file_has_suffix(paths.paths[i], ".bhv")) continue;
        const char *relative = relative_path(mod, paths.paths[i]);
        if (!relative) continue;
        if (loaded >= MOD_MAX_DYNOS_BEHAVIORS) {
            mod_logf("warning", mod->name, "stopped DynOS behavior discovery at the %u-resource limit", MOD_MAX_DYNOS_BEHAVIORS);
            break;
        }
        if (load_dynos_behavior_file(mod, relative)) ++loaded;
    }
    fs_pathlist_free(&paths);
}

static void resolve_dynos_behavior_functions(Mod *mod) {
    if (!sLua || mod->env_ref == LUA_NOREF) return;
    for (ModBehavior *behavior = mod->behaviors; behavior; behavior = behavior->next_owned) {
        if (!behavior->dynos_behavior || !behavior->lua_token_count) continue;
        lua_rawgeti(sLua, LUA_REGISTRYINDEX, mod->env_ref);
        int environment = lua_gettop(sLua);
        for (size_t i = 0; i < behavior->lua_token_count; ++i) {
            lua_getfield(sLua, environment, behavior->lua_tokens[i]);
            if (!lua_isfunction(sLua, -1)) {
                lua_pop(sLua, 1);
                behavior->enabled = false;
                mod_logf("error", mod->name, "DynOS behavior %s requires Lua function '%s' from %s",
                         behavior->name, behavior->lua_tokens[i], behavior->file);
                break;
            }
            behavior->lua_function_refs[i] = luaL_ref(sLua, LUA_REGISTRYINDEX);
        }
        lua_pop(sLua, 1);
    }
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

static bool add_action(Mod *owner, uint32_t id, uint32_t interaction_type, int every_ref, int gravity_ref) {
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
    action->owner = owner; action->id = id; action->interaction_type = interaction_type;
    action->every_frame_ref = every_ref;
    action->gravity_ref = gravity_ref; action->enabled = true;
    action->next_global = sActions; sActions = action;
    action->next_owned = owner->actions; owner->actions = action;
    return true;
}

static void refresh_local_player_tables(void) {
    if (!sLua) return;
    lua_getglobal(sLua, "gNetworkPlayers");
    if (!lua_istable(sLua, -1)) { lua_pop(sLua, 1); return; }
    lua_rawgeti(sLua, -1, 0);
    if (lua_istable(sLua, -1)) {
        lua_pushinteger(sLua, 0); lua_setfield(sLua, -2, "globalIndex");
        lua_pushinteger(sLua, gCurrCourseNum); lua_setfield(sLua, -2, "currCourseNum");
        lua_pushinteger(sLua, gCurrLevelNum); lua_setfield(sLua, -2, "currLevelNum");
        lua_pushinteger(sLua, gCurrAreaIndex); lua_setfield(sLua, -2, "currAreaIndex");
        lua_pushboolean(sLua, true); lua_setfield(sLua, -2, "connected");
    }
    lua_pop(sLua, 2);
}

static void describe_nil_callback_upvalues(lua_State *L, int function_index,
                                          char *buffer, size_t buffer_size) {
    size_t used = 0;
    if (!buffer_size) return;
    buffer[0] = '\0';
    function_index = lua_absindex(L, function_index);
    for (int index = 1; index <= 64; ++index) {
        const char *name = lua_getupvalue(L, function_index, index);
        if (!name) break;
        bool is_nil = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!is_nil) continue;
        int written = snprintf(buffer + used, buffer_size - used, "%s%d",
                              used ? "," : "", index);
        if (written < 0 || (size_t)written >= buffer_size - used) break;
        used += (size_t)written;
    }
}

static int mod_callback_error_handler(lua_State *L) {
    for (int level = 0; level < 32; ++level) {
        lua_Debug frame;
        if (!lua_getstack(L, level, &frame)) break;
        if (!lua_getinfo(L, "nSlf", &frame)) continue;
        int function_index = lua_gettop(L);
        char nils[96];
        describe_nil_callback_upvalues(L, function_index, nils, sizeof(nils));
        fprintf(stderr, "[mods][trace] %s:%d (defined %d) %s%s%s\n",
                frame.short_src, frame.currentline,
                frame.linedefined,
                frame.name ? frame.name : "<anonymous>", nils[0] ? " nil upvalues=" : "",
                nils[0] ? nils : "");
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 1);
    return 1;
}

/* Common callback entry that preserves a requested number of return values. */
static int call_ref(Mod *owner, const char *file, int function_ref, const char *event, int nargs, int nresults) {
    lua_State *L = sLua;
    refresh_local_player_tables();
    int base = lua_gettop(L) - nargs;
    lua_rawgeti(L, LUA_REGISTRYINDEX, function_ref);
    lua_insert(L, base + 1);
    lua_pushcfunction(L, mod_callback_error_handler);
    lua_insert(L, base + 1);
    char nil_upvalues[192];
    describe_nil_callback_upvalues(L, base + 2, nil_upvalues, sizeof(nil_upvalues));
    Mod *previous_mod = sCurrentMod;
    const char *previous_file = sCurrentFile;
    sCurrentMod = owner;
    sCurrentFile = file;
    begin_lua_callback(L);
    int status = lua_pcall(L, nargs, nresults, base + 1);
    lua_remove(L, base + 1);
    end_lua_callback();
    sCurrentMod = previous_mod;
    sCurrentFile = previous_file;
    if (status != LUA_OK) {
        const char *message = lua_tostring(L, -1);
        fprintf(stderr, "[mods][error][%s] %s (%s) callback: %s%s%s%s\n", owner->name, file, event,
                message ? message : "unknown Lua error",
                nil_upvalues[0] ? " (nil callback upvalues: " : "",
                nil_upvalues[0] ? nil_upvalues : "",
                nil_upvalues[0] ? ")" : "");
        lua_pop(L, 1); ++sHookFailures;
        return 0;
    }
    return 1;
}

static void push_mario_index(lua_State *L, int player_index) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_MARIO, .field = player_index };
    luaL_getmetatable(L, "sm64.mod.MarioState"); lua_setmetatable(L, -2);
}

static void push_mario(lua_State *L) { push_mario_index(L, 0); }

static void push_mario_body_state(lua_State *L, struct MarioState *m) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_MARIO_BODY_STATE, .pointer = (uintptr_t)m };
    luaL_getmetatable(L, "sm64.mod.MarioBodyState");
    lua_setmetatable(L, -2);
}

static void push_player_camera_state(lua_State *L, struct MarioState *m) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_PLAYER_CAMERA_STATE, .pointer = (uintptr_t)m };
    luaL_getmetatable(L, "sm64.mod.PlayerCameraState");
    lua_setmetatable(L, -2);
}

static void push_camera(lua_State *L) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_CAMERA };
    luaL_getmetatable(L, "sm64.mod.Camera"); lua_setmetatable(L, -2);
}

static void push_camera_reference(lua_State *L, struct Camera *camera) {
    if (!camera) { lua_pushnil(L); return; }
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_CAMERA, .pointer = (uintptr_t)camera };
    luaL_getmetatable(L, "sm64.mod.Camera"); lua_setmetatable(L, -2);
}

static void push_area(lua_State *L, struct Area *area) {
    if (!area) { lua_pushnil(L); return; }
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_AREA, .pointer = (uintptr_t)area };
    luaL_getmetatable(L, "sm64.mod.Area"); lua_setmetatable(L, -2);
}

static void push_lakitu_state(lua_State *L) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_LAKITU_STATE, .pointer = (uintptr_t)&gLakituState };
    luaL_getmetatable(L, "sm64.mod.LakituState"); lua_setmetatable(L, -2);
}

static ModObject *find_object(struct Object *object) {
    for (ModObject *entry = sObjects; entry; entry = entry->next) if (entry->object == object && entry->enabled) return entry;
    return NULL;
}

static ObjectReference *object_reference(struct Object *object, bool create) {
    ObjectReference *empty = NULL;
    for (size_t i = 0; i < OBJECT_POOL_CAPACITY; ++i) {
        ObjectReference *ref = &sObjectRefs[i];
        if (ref->object == object) return ref;
        if (!ref->object && !empty) empty = ref;
    }
    if (!create || !empty) return NULL;
    *empty = (ObjectReference){ .object = object, .token = sNextObjectToken++ };
    memcpy(empty->shadow_position, object->header.gfx.pos, sizeof(empty->shadow_position));
    if (!sNextObjectToken) sNextObjectToken = 1;
    return empty;
}

static bool object_reference_valid(const LuaUserdata *ud) {
    if (!ud || !ud->object) return false;
    ObjectReference *ref = object_reference(ud->object, false);
    return ref && ref->token == ud->token &&
           ref->object->activeFlags != ACTIVE_FLAG_DEACTIVATED;
}

static uint8_t object_render_hook_id(struct Object *object) {
    ObjectReference *ref = object_reference(object, false);
    if (!ref || object->activeFlags == ACTIVE_FLAG_DEACTIVATED) return 0;
    for (ModObjectRenderOwner *owner = sRenderHookOwners; owner; owner = owner->next)
        if (owner->object == object && owner->token == ref->token && owner->owner->enabled)
            return owner->render_id;
    return 0;
}

static bool object_has_render_hook(struct Object *object) {
    return object_render_hook_id(object) != 0;
}

static void remove_render_hook_owners(Mod *owner, struct Object *object) {
    ModObjectRenderOwner **link = &sRenderHookOwners;
    while (*link) {
        ModObjectRenderOwner *entry = *link;
        if ((!owner || entry->owner == owner) && (!object || entry->object == object)) {
            *link = entry->next;
            free(entry);
            if (sRenderHookOwnerCount) --sRenderHookOwnerCount;
        } else link = &entry->next;
    }
}

static bool set_object_render_hook(lua_State *L, struct Object *object, lua_Integer render_id) {
    Mod *owner = sCurrentMod;
    if (!owner) {
        luaL_error(L, "Object.hookRender can only be changed while a mod callback is running");
        return false;
    }
    ObjectReference *ref = object_reference(object, false);
    if (!ref || object->activeFlags == ACTIVE_FLAG_DEACTIVATED) {
        luaL_error(L, "Object.hookRender requires a live object reference");
        return false;
    }
    if (render_id < 0 || render_id > UINT8_MAX) {
        luaL_argerror(L, 3, "Object.hookRender must fit in the native u8 render-hook field");
        return false;
    }

    unsigned owned = 0;
    for (ModObjectRenderOwner *entry = sRenderHookOwners; entry; entry = entry->next) {
        if (entry->object == object && entry->owner == owner) {
            entry->render_id = (uint8_t)render_id;
            return true;
        }
        if (entry->owner == owner) ++owned;
    }
    if (sRenderHookOwnerCount >= MOD_MAX_RENDER_HOOK_OWNERS || owned >= MOD_MAX_OWNED_OBJECTS) {
        luaL_error(L, "mod-owned Object.hookRender limit reached (%d per mod, %d total)",
                   (int)MOD_MAX_OWNED_OBJECTS, (int)MOD_MAX_RENDER_HOOK_OWNERS);
        return false;
    }
    ModObjectRenderOwner *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        luaL_error(L, "out of memory registering Object.hookRender");
        return false;
    }
    entry->owner = owner;
    entry->object = object;
    entry->token = ref->token;
    entry->render_id = (uint8_t)render_id;
    entry->next = sRenderHookOwners;
    sRenderHookOwners = entry;
    ++sRenderHookOwnerCount;
    return true;
}

static void restore_mod_visibility(Mod *owner) {
    for (size_t i = 0; i < OBJECT_POOL_CAPACITY; ++i) {
        ObjectReference *ref = &sObjectRefs[i];
        if (ref->visibility_owner != owner) continue;
        if (ref->object->activeFlags != ACTIVE_FLAG_DEACTIVATED) {
            s16 *flags = &ref->object->header.gfx.node.flags;
            if (((*flags & GRAPH_RENDER_ACTIVE) != 0) == ref->written_visible) {
                if (ref->original_visible) *flags |= GRAPH_RENDER_ACTIVE;
                else *flags &= ~GRAPH_RENDER_ACTIVE;
            }
        }
        ref->visibility_owner = NULL;
    }
}

static const BehaviorScript *object_behavior_script(struct Object *object) {
    ModObject *entry = find_object(object);
    if (entry && entry->behavior) return (const BehaviorScript *)entry->behavior->script;
    return object ? object->behavior : NULL;
}

static void push_object(lua_State *L, struct Object *object) {
    ObjectReference *ref = object_reference(object, true);
    if (!ref) { lua_pushnil(L); return; }
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_OBJECT, .object = object, .token = ref->token };
    luaL_getmetatable(L, "sm64.mod.Object"); lua_setmetatable(L, -2);
}

static void push_behavior_pointer(lua_State *L, const BehaviorScript *script) {
    if (!script) { lua_pushnil(L); return; }
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_POINTER, .field = MOD_POINTER_BEHAVIOR,
                         .pointer = (uintptr_t)script };
    luaL_getmetatable(L, "sm64.mod.Pointer");
    lua_setmetatable(L, -2);
}

static void push_collision_pointer(lua_State *L, const Collision *collision) {
    if (!collision) { lua_pushnil(L); return; }
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_POINTER, .field = MOD_POINTER_COLLISION,
                         .pointer = (uintptr_t)collision };
    luaL_getmetatable(L, "sm64.mod.Pointer");
    lua_setmetatable(L, -2);
}

static void push_object_proxy(lua_State *L, LuaUserdataKind kind, struct Object *object, uint32_t token) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    struct GraphNode *graph_node = kind == LUA_UD_GRAPH_NODE ? &object->header.gfx.node : NULL;
    *ud = (LuaUserdata){ .kind = kind, .object = object, .token = token, .graph_node = graph_node };
    const char *metatable = kind == LUA_UD_OBJECT_HEADER ? "sm64.mod.ObjectHeader" :
                            kind == LUA_UD_OBJECT_GFX ? "sm64.mod.ObjectGfx" :
                            kind == LUA_UD_ANIMATION_INFO ? "sm64.mod.AnimationInfo" : "sm64.mod.GraphNode";
    luaL_getmetatable(L, metatable); lua_setmetatable(L, -2);
}

static ModAnimation *find_animation_pointer(const struct Animation *animation) {
    for (ModAnimation *it = sAnimations; it; it = it->next_global)
        if (it->animation == animation) return it;
    return NULL;
}

static void push_animation(lua_State *L, struct Animation *animation) {
    if (!animation) { lua_pushnil(L); return; }
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_ANIMATION, .pointer = (uintptr_t)animation,
                         .field = find_animation_pointer(animation) ? 1 : 0 };
    luaL_getmetatable(L, "sm64.mod.Animation"); lua_setmetatable(L, -2);
}

static void push_graph_node_object(lua_State *L, struct GraphNodeObject *graph,
                                   float *shadow_position, bool *disable_automatic_shadow_position) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_GRAPH_NODE_OBJECT, .graph_object = graph,
                         .shadow_position = shadow_position,
                         .disable_automatic_shadow_position = disable_automatic_shadow_position };
    luaL_getmetatable(L, "sm64.mod.GraphNodeObject"); lua_setmetatable(L, -2);
}

static void push_graph_node(lua_State *L, struct GraphNode *node) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_GRAPH_NODE, .graph_node = node };
    luaL_getmetatable(L, "sm64.mod.GraphNode"); lua_setmetatable(L, -2);
}

static void push_direct_vector(lua_State *L, bool is_float, void *values) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = is_float ? LUA_UD_VEC_F : LUA_UD_VEC_S,
                         .field = is_float ? VEC_DIRECT_F : VEC_DIRECT_S };
    if (is_float) ud->float_vector = values;
    else ud->short_vector = values;
    luaL_getmetatable(L, is_float ? "sm64.mod.Vec3f" : "sm64.mod.Vec3s");
    lua_setmetatable(L, -2);
}

static void push_vec3_snapshot(lua_State *L, const float values[3]) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_VEC_F_SNAPSHOT };
    memcpy(ud->vector_snapshot, values, sizeof(ud->vector_snapshot));
    luaL_getmetatable(L, "sm64.mod.Vec3f");
    lua_setmetatable(L, -2);
}

static void push_vec3s_snapshot(lua_State *L, const int16_t values[3]) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_VEC_S_SNAPSHOT };
    memcpy(ud->short_vector_snapshot, values, sizeof(ud->short_vector_snapshot));
    luaL_getmetatable(L, "sm64.mod.Vec3s");
    lua_setmetatable(L, -2);
}

static void push_gfx_offset(lua_State *L, struct Object *object, uint32_t token) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_GFX_OFFSET, .object = object, .token = token };
    luaL_getmetatable(L, "sm64.mod.GfxOffset"); lua_setmetatable(L, -2);
}

static void push_object_camera_vector(lua_State *L, struct Object *object, uint32_t token) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_VEC_F, .field = VEC_OBJECT_CAMERA_TO_OBJECT,
                         .object = object, .token = token };
    luaL_getmetatable(L, "sm64.mod.Vec3f"); lua_setmetatable(L, -2);
}

static void push_object_shadow_vector(lua_State *L, struct Object *object, uint32_t token) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_VEC_F, .field = VEC_OBJECT_SHADOW_POSITION,
                         .object = object, .token = token };
    luaL_getmetatable(L, "sm64.mod.Vec3f"); lua_setmetatable(L, -2);
}

static void push_surface(lua_State *L, const struct Surface *surface) {
    if (!surface) { lua_pushnil(L); return; }
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    memset(ud, 0, sizeof(*ud));
    ud->kind = LUA_UD_SURFACE;
    ud->surface = *surface;
    luaL_getmetatable(L, "sm64.mod.Surface"); lua_setmetatable(L, -2);
}

static void push_vector(lua_State *L, LuaUserdataKind kind, int field) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = kind, .field = field };
    luaL_getmetatable(L, kind == LUA_UD_VEC_F ? "sm64.mod.Vec3f" : "sm64.mod.Vec3s");
    lua_setmetatable(L, -2);
}

static struct Camera *checked_camera_userdata(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.Camera");
    if (ud->kind != LUA_UD_CAMERA) luaL_error(L, "expected a Camera argument");
    struct Camera *camera = ud->pointer ? (struct Camera *)ud->pointer : gCamera;
    if (!camera) luaL_error(L, "active native camera is unavailable");
    return camera;
}

static int camera_index(lua_State *L) {
    struct Camera *camera = checked_camera_userdata(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "mode")) lua_pushinteger(L, camera->mode);
    else if (!strcmp(key, "defMode")) lua_pushinteger(L, camera->defMode);
    else if (!strcmp(key, "yaw")) lua_pushinteger(L, camera->yaw);
    else if (!strcmp(key, "nextYaw")) lua_pushinteger(L, camera->nextYaw);
    else if (!strcmp(key, "cutscene")) lua_pushinteger(L, camera->cutscene);
    else if (!strcmp(key, "doorStatus")) lua_pushinteger(L, camera->doorStatus);
    else if (!strcmp(key, "areaCenX")) lua_pushnumber(L, camera->areaCenX);
    else if (!strcmp(key, "areaCenY")) lua_pushnumber(L, camera->areaCenY);
    else if (!strcmp(key, "areaCenZ")) lua_pushnumber(L, camera->areaCenZ);
    else if (!strcmp(key, "pos")) push_direct_vector(L, true, camera->pos);
    else if (!strcmp(key, "focus")) push_direct_vector(L, true, camera->focus);
    else return luaL_error(L, "Camera.%s is unsupported by this adapter", key);
    return 1;
}

static int camera_newindex(lua_State *L) {
    struct Camera *camera = checked_camera_userdata(L, 1);
    const char *key = luaL_checkstring(L, 2);
    lua_Integer integer;
    if (!strcmp(key, "mode") || !strcmp(key, "defMode") ||
        !strcmp(key, "cutscene") || !strcmp(key, "doorStatus")) {
        integer = luaL_checkinteger(L, 3);
        if (integer < 0 || integer > UINT8_MAX)
            return luaL_argerror(L, 3, "camera byte field must fit in u8");
        if (!strcmp(key, "mode")) camera->mode = (u8)integer;
        else if (!strcmp(key, "defMode")) camera->defMode = (u8)integer;
        else if (!strcmp(key, "cutscene")) camera->cutscene = (u8)integer;
        else camera->doorStatus = (u8)integer;
        return 0;
    }
    if (!strcmp(key, "yaw") || !strcmp(key, "nextYaw")) {
        integer = luaL_checkinteger(L, 3);
        if (integer < INT16_MIN || integer > INT16_MAX)
            return luaL_argerror(L, 3, "camera angle must fit in s16");
        if (!strcmp(key, "yaw")) camera->yaw = (s16)integer;
        else camera->nextYaw = (s16)integer;
        return 0;
    }
    if (!strcmp(key, "areaCenX") || !strcmp(key, "areaCenY") || !strcmp(key, "areaCenZ")) {
        float value = checked_lua_finite_float(L, 3, key);
        if (!strcmp(key, "areaCenX")) camera->areaCenX = value;
        else if (!strcmp(key, "areaCenY")) camera->areaCenY = value;
        else camera->areaCenZ = value;
        return 0;
    }
    return luaL_error(L, "Camera.%s is read-only or unsupported by this adapter", key);
}

static struct Area *checked_area_userdata(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.Area");
    if (ud->kind != LUA_UD_AREA || !ud->pointer) luaL_error(L, "expected a live Area reference");
    return (struct Area *)ud->pointer;
}

static int area_index(lua_State *L) {
    struct Area *area = checked_area_userdata(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "index")) lua_pushinteger(L, area->index);
    else if (!strcmp(key, "flags")) lua_pushinteger(L, area->flags);
    else if (!strcmp(key, "terrainType")) lua_pushinteger(L, area->terrainType);
    else if (!strcmp(key, "camera")) push_camera_reference(L, area->camera);
    else return luaL_error(L, "Area.%s is unsupported by this adapter", key);
    return 1;
}

static int area_newindex(lua_State *L) {
    return luaL_error(L, "Area fields are read-only in the iOS compatibility adapter");
}

static struct LakituState *checked_lakitu_state(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.LakituState");
    if (ud->kind != LUA_UD_LAKITU_STATE || ud->pointer != (uintptr_t)&gLakituState)
        luaL_error(L, "invalid LakituState reference");
    return &gLakituState;
}

static int lakitu_state_index(lua_State *L) {
    struct LakituState *state = checked_lakitu_state(L, 1);
    const char *key = luaL_checkstring(L, 2);
#define LAKITU_GET_S16(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, state->field); return 1; }
#define LAKITU_GET_U8(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, state->field); return 1; }
#define LAKITU_GET_F32(name, field) if (!strcmp(key, name)) { lua_pushnumber(L, state->field); return 1; }
    if (!strcmp(key, "curFocus")) push_direct_vector(L, true, state->curFocus);
    else if (!strcmp(key, "curPos")) push_direct_vector(L, true, state->curPos);
    else if (!strcmp(key, "goalFocus")) push_direct_vector(L, true, state->goalFocus);
    else if (!strcmp(key, "goalPos")) push_direct_vector(L, true, state->goalPos);
    else if (!strcmp(key, "unusedVec1")) push_direct_vector(L, true, state->unusedVec1);
    else if (!strcmp(key, "unusedVec2")) push_direct_vector(L, false, state->unusedVec2);
    else if (!strcmp(key, "shakeMagnitude")) push_direct_vector(L, false, state->shakeMagnitude);
    else if (!strcmp(key, "focus")) push_direct_vector(L, true, state->focus);
    else if (!strcmp(key, "pos")) push_direct_vector(L, true, state->pos);
    else if (!strcmp(key, "lastFrameAction")) lua_pushinteger(L, state->lastFrameAction);
    else {
        LAKITU_GET_U8("mode", mode)
        else LAKITU_GET_U8("defMode", defMode)
        else LAKITU_GET_F32("focusDistance", focusDistance)
        else LAKITU_GET_S16("oldPitch", oldPitch)
        else LAKITU_GET_S16("oldYaw", oldYaw)
        else LAKITU_GET_S16("oldRoll", oldRoll)
        else LAKITU_GET_S16("shakePitchPhase", shakePitchPhase)
        else LAKITU_GET_S16("shakePitchVel", shakePitchVel)
        else LAKITU_GET_S16("shakePitchDecay", shakePitchDecay)
        else LAKITU_GET_S16("roll", roll)
        else LAKITU_GET_S16("yaw", yaw)
        else LAKITU_GET_S16("nextYaw", nextYaw)
        else LAKITU_GET_S16("shakeRollPhase", shakeRollPhase)
        else LAKITU_GET_S16("shakeRollVel", shakeRollVel)
        else LAKITU_GET_S16("shakeRollDecay", shakeRollDecay)
        else LAKITU_GET_S16("shakeYawPhase", shakeYawPhase)
        else LAKITU_GET_S16("shakeYawVel", shakeYawVel)
        else LAKITU_GET_S16("shakeYawDecay", shakeYawDecay)
        else LAKITU_GET_S16("keyDanceRoll", keyDanceRoll)
        else LAKITU_GET_S16("unused", unused)
        else LAKITU_GET_F32("focHSpeed", focHSpeed)
        else LAKITU_GET_F32("focVSpeed", focVSpeed)
        else LAKITU_GET_F32("posHSpeed", posHSpeed)
        else LAKITU_GET_F32("posVSpeed", posVSpeed)
        else return luaL_error(L, "LakituState.%s is unsupported by this adapter", key);
    }
#undef LAKITU_GET_S16
#undef LAKITU_GET_U8
#undef LAKITU_GET_F32
    return 1;
}

static int lakitu_state_newindex(lua_State *L) {
    struct LakituState *state = checked_lakitu_state(L, 1);
    const char *key = luaL_checkstring(L, 2);
    lua_Integer integer;
    if (!strcmp(key, "mode") || !strcmp(key, "defMode")) {
        integer = luaL_checkinteger(L, 3);
        if (integer < 0 || integer > UINT8_MAX) return luaL_argerror(L, 3, "camera mode must fit in u8");
        if (!strcmp(key, "mode")) state->mode = (u8)integer;
        else state->defMode = (u8)integer;
        return 0;
    }
    if (!strcmp(key, "lastFrameAction")) {
        integer = luaL_checkinteger(L, 3);
        if (integer < 0 || (uint64_t)integer > UINT32_MAX)
            return luaL_argerror(L, 3, "lastFrameAction must fit in u32");
        state->lastFrameAction = (u32)integer;
        return 0;
    }
#define LAKITU_SET_S16(name, field) \
    if (!strcmp(key, name)) { \
        integer = luaL_checkinteger(L, 3); \
        if (integer < INT16_MIN || integer > INT16_MAX) return luaL_argerror(L, 3, "camera angle must fit in s16"); \
        state->field = (s16)integer; \
        return 0; \
    }
#define LAKITU_SET_F32(name, field) \
    if (!strcmp(key, name)) { state->field = checked_lua_finite_float(L, 3, key); return 0; }
    LAKITU_SET_F32("focusDistance", focusDistance)
    LAKITU_SET_S16("oldPitch", oldPitch)
    LAKITU_SET_S16("oldYaw", oldYaw)
    LAKITU_SET_S16("oldRoll", oldRoll)
    LAKITU_SET_S16("shakePitchPhase", shakePitchPhase)
    LAKITU_SET_S16("shakePitchVel", shakePitchVel)
    LAKITU_SET_S16("shakePitchDecay", shakePitchDecay)
    LAKITU_SET_S16("roll", roll)
    LAKITU_SET_S16("yaw", yaw)
    LAKITU_SET_S16("nextYaw", nextYaw)
    LAKITU_SET_S16("shakeRollPhase", shakeRollPhase)
    LAKITU_SET_S16("shakeRollVel", shakeRollVel)
    LAKITU_SET_S16("shakeRollDecay", shakeRollDecay)
    LAKITU_SET_S16("shakeYawPhase", shakeYawPhase)
    LAKITU_SET_S16("shakeYawVel", shakeYawVel)
    LAKITU_SET_S16("shakeYawDecay", shakeYawDecay)
    LAKITU_SET_S16("keyDanceRoll", keyDanceRoll)
    LAKITU_SET_S16("unused", unused)
    LAKITU_SET_F32("focHSpeed", focHSpeed)
    LAKITU_SET_F32("focVSpeed", focVSpeed)
    LAKITU_SET_F32("posHSpeed", posHSpeed)
    LAKITU_SET_F32("posVSpeed", posVSpeed)
#undef LAKITU_SET_S16
#undef LAKITU_SET_F32
    return luaL_error(L, "LakituState.%s is read-only or unsupported by this adapter", key);
}

static struct MarioState *current_mario(void) { return gMarioState; }

static float *vec_f_pointer(LuaUserdata *ud) {
    if (ud->field == VEC_DIRECT_F) return ud->float_vector;
    switch (ud->field) {
        case VEC_GLOBAL_SOUND_SOURCE: return gGlobalSoundSource;
        case VEC_DEFAULT_SOUND_ARGS: return gDefaultSoundArgs;
        case VEC_OBJECT_CAMERA_TO_OBJECT:
            return object_reference_valid(ud) ? ud->object->header.gfx.cameraToObject : NULL;
        case VEC_OBJECT_SHADOW_POSITION: {
            ObjectReference *ref = object_reference_valid(ud) ? object_reference(ud->object, false) : NULL;
            return ref ? ref->shadow_position : NULL;
        }
        default: break;
    }
    struct MarioState *m = current_mario();
    if (!m) return NULL;
    switch (ud->field) {
        case VEC_MARIO_POS: return m->pos;
        case VEC_MARIO_VEL: return m->vel;
        default: return NULL;
    }
}

static int16_t *vec_s_pointer(LuaUserdata *ud) {
    if (ud->field == VEC_DIRECT_S) return ud->short_vector;
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
    if (ud->kind == LUA_UD_VEC_F || ud->kind == LUA_UD_VEC_F_SNAPSHOT) {
        float *v = ud->kind == LUA_UD_VEC_F_SNAPSHOT ? ud->vector_snapshot : vec_f_pointer(ud);
        if (ud->kind == LUA_UD_VEC_F &&
            (ud->field == VEC_OBJECT_CAMERA_TO_OBJECT || ud->field == VEC_OBJECT_SHADOW_POSITION) && !v)
            return luaL_error(L, "stale Object vector reference");
        lua_pushnumber(L, v ? v[axis] : 0);
    } else {
        int16_t *v = ud->kind == LUA_UD_VEC_S_SNAPSHOT ? ud->short_vector_snapshot : vec_s_pointer(ud);
        lua_pushinteger(L, v ? v[axis] : 0);
    }
    return 1;
}

static int vector_newindex(lua_State *L) {
    LuaUserdata *ud = lua_touserdata(L, 1);
    if (ud->kind == LUA_UD_VEC_F_SNAPSHOT || ud->kind == LUA_UD_VEC_S_SNAPSHOT)
        return luaL_error(L, "this vector is a read-only snapshot");
    int axis = 0;
    if (lua_isinteger(L, 2)) axis = (int)lua_tointeger(L, 2) - 1;
    else {
        const char *key = luaL_checkstring(L, 2);
        axis = key[0] == 'x' ? 0 : key[0] == 'y' ? 1 : key[0] == 'z' ? 2 : -1;
    }
    if (axis < 0 || axis > 2) return luaL_error(L, "vector index must be x/y/z or 1..3");
    if (ud->kind == LUA_UD_VEC_F) {
        float *v = vec_f_pointer(ud);
        if ((ud->field == VEC_OBJECT_CAMERA_TO_OBJECT || ud->field == VEC_OBJECT_SHADOW_POSITION) && !v)
            return luaL_error(L, "stale Object vector reference");
        if (v) v[axis] = (float)luaL_checknumber(L, 3);
    }
    else { int16_t *v = vec_s_pointer(ud); if (v) v[axis] = (int16_t)luaL_checkinteger(L, 3); }
    return 0;
}

static struct Object *checked_object(lua_State *L, int index);

static bool mario_number_get(struct MarioState *m, const char *key, lua_State *L) {
#define GET_U32(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, (lua_Integer)m->field); return true; }
#define GET_S32(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, (lua_Integer)m->field); return true; }
#define GET_NUM(name, field) if (!strcmp(key, name)) { lua_pushnumber(L, m->field); return true; }
    GET_U32("action", action) GET_U32("prevAction", prevAction) GET_U32("actionArg", actionArg)
    GET_U32("flags", flags) GET_U32("particleFlags", particleFlags) GET_U32("input", input)
    GET_U32("actionState", actionState) GET_U32("actionTimer", actionTimer)
    GET_U32("wallKickTimer", wallKickTimer) GET_S32("numCoins", numCoins)
    GET_U32("capTimer", capTimer)
    GET_S32("numStars", numStars) GET_S32("numLives", numLives) GET_S32("health", health)
    GET_NUM("forwardVel", forwardVel) GET_NUM("slideVelX", slideVelX) GET_NUM("slideVelZ", slideVelZ)
    GET_NUM("intendedMag", intendedMag) GET_NUM("peakHeight", peakHeight)
    GET_S32("intendedYaw", intendedYaw)
    if (!strcmp(key, "specialTripleJump")) { lua_pushinteger(L, gSpecialTripleJump); return true; }
    GET_S32("floorAngle", floorAngle) GET_S32("floorHeight", floorHeight)
    GET_S32("waterLevel", waterLevel) GET_S32("hurtCounter", hurtCounter)
    GET_S32("terrainSoundAddend", terrainSoundAddend)
    GET_S32("squishTimer", squishTimer)
    GET_S32("framesSinceA", framesSinceA)
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
    SET_INT("capTimer", capTimer, uint16_t)
    SET_INT("wallKickTimer", wallKickTimer, uint8_t) SET_INT("health", health, int16_t)
    SET_INT("numCoins", numCoins, int16_t) SET_INT("numStars", numStars, int16_t)
    SET_INT("numLives", numLives, int8_t) SET_INT("intendedYaw", intendedYaw, int16_t)
    SET_INT("floorAngle", floorAngle, int16_t) SET_INT("hurtCounter", hurtCounter, uint8_t)
    SET_INT("framesSinceA", framesSinceA, uint8_t)
    if (!strcmp(key, "specialTripleJump")) {
        gSpecialTripleJump = (u8)luaL_checkinteger(L, value_index);
        return true;
    }
    SET_NUM("forwardVel", forwardVel) SET_NUM("slideVelX", slideVelX) SET_NUM("slideVelZ", slideVelZ)
    SET_NUM("intendedMag", intendedMag) SET_NUM("peakHeight", peakHeight)
#undef SET_INT
#undef SET_NUM
    return false;
}

static int mario_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.MarioState");
    int player_index = ud->field;
    struct MarioState *m = current_mario();
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "playerIndex")) { lua_pushinteger(L, player_index); return 1; }
    if (player_index != 0) { lua_pushnil(L); return 1; }
    if (!m) { lua_pushnil(L); return 1; }
    if (!strcmp(key, "character")) push_native_character(L);
    else if (!strcmp(key, "floor")) push_surface(L, m->floor);
    else if (!strcmp(key, "area")) push_area(L, m->area);
    else if (!strcmp(key, "pos")) push_vector(L, LUA_UD_VEC_F, VEC_MARIO_POS);
    else if (!strcmp(key, "vel")) push_vector(L, LUA_UD_VEC_F, VEC_MARIO_VEL);
    else if (!strcmp(key, "faceAngle")) push_vector(L, LUA_UD_VEC_S, VEC_MARIO_FACE_ANGLE);
    else if (!strcmp(key, "angleVel")) push_vector(L, LUA_UD_VEC_S, VEC_MARIO_ANGLE_VEL);
    else if (!strcmp(key, "controller") && m->controller) {
        LuaUserdata *c = lua_newuserdata(L, sizeof(*c)); *c = (LuaUserdata){ .kind = LUA_UD_CONTROLLER };
        luaL_getmetatable(L, "sm64.mod.Controller"); lua_setmetatable(L, -2);
    } else if (!strcmp(key, "marioObj")) {
        if (m->marioObj) push_object(L, m->marioObj); else lua_pushnil(L);
    } else if (!strcmp(key, "heldObj")) {
        if (m->heldObj) push_object(L, m->heldObj); else lua_pushnil(L);
    } else if (!strcmp(key, "interactObj")) {
        if (m->interactObj) push_object(L, m->interactObj); else lua_pushnil(L);
    } else if (!strcmp(key, "usedObj")) {
        if (m->usedObj) push_object(L, m->usedObj); else lua_pushnil(L);
    } else if (!strcmp(key, "riddenObj")) {
        if (m->riddenObj) push_object(L, m->riddenObj); else lua_pushnil(L);
    } else if (!strcmp(key, "marioBodyState")) {
        if (m->marioBodyState) push_mario_body_state(L, m); else lua_pushnil(L);
    } else if (!strcmp(key, "statusForCamera")) {
        if (m->statusForCamera) push_player_camera_state(L, m); else lua_pushnil(L);
    }
    else if (!mario_number_get(m, key, L)) return luaL_error(L, "MarioState field '%s' is unsupported by this adapter", key);
    return 1;
}

static int mario_newindex(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.MarioState");
    if (ud->field != 0) return luaL_error(L, "MarioState[%d] has no native local player in single-player mode", ud->field);
    struct MarioState *m = current_mario();
    const char *key = luaL_checkstring(L, 2);
    if (!m) return luaL_error(L, "Mario state is unavailable");
    struct Object **object_field = NULL;
    if (!strcmp(key, "heldObj")) object_field = &m->heldObj;
    else if (!strcmp(key, "interactObj")) object_field = &m->interactObj;
    else if (!strcmp(key, "usedObj")) object_field = &m->usedObj;
    else if (!strcmp(key, "riddenObj")) object_field = &m->riddenObj;
    if (object_field) {
        if (lua_isnil(L, 3)) *object_field = NULL;
        else *object_field = checked_object(L, 3);
        return 0;
    }
    if (!mario_number_set(m, key, L, 3)) return luaL_error(L, "MarioState field '%s' is read-only or unsupported", key);
    return 0;
}

static struct MarioBodyState *checked_mario_body_state(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.MarioBodyState");
    struct MarioState *owner = (struct MarioState *)ud->pointer;
    if (!owner || owner != current_mario())
        luaL_error(L, "stale MarioBodyState reference");
    if (!owner->marioBodyState)
        luaL_error(L, "MarioBodyState is unavailable");
    return owner->marioBodyState;
}

static int mario_body_state_index(lua_State *L) {
    struct MarioBodyState *body = checked_mario_body_state(L, 1);
    const char *key = luaL_checkstring(L, 2);
#define GET_BODY_U32(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, (lua_Integer)body->field); return 1; }
#define GET_BODY_S8(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, body->field); return 1; }
#define GET_BODY_U8(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, body->field); return 1; }
#define GET_BODY_S16(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, body->field); return 1; }
    GET_BODY_U32("action", action)
    GET_BODY_S8("capState", capState)
    GET_BODY_S8("eyeState", eyeState)
    GET_BODY_S8("handState", handState)
    GET_BODY_S8("wingFlutter", wingFlutter)
    GET_BODY_S16("modelState", modelState)
    GET_BODY_S8("grabPos", grabPos)
    GET_BODY_U8("punchState", punchState)
#undef GET_BODY_U32
#undef GET_BODY_S8
#undef GET_BODY_U8
#undef GET_BODY_S16
    if (!strcmp(key, "torsoAngle")) {
        push_vec3s_snapshot(L, body->torsoAngle);
        return 1;
    }
    if (!strcmp(key, "headAngle")) {
        push_vec3s_snapshot(L, body->headAngle);
        return 1;
    }
    if (!strcmp(key, "heldObjLastPosition")) {
        push_vec3_snapshot(L, body->heldObjLastPosition);
        return 1;
    }
    /* CoopDX exposes additional body/render state that is absent in this engine's struct. */
    if (!strcmp(key, "allowPartRotation") || !strcmp(key, "mirrorMario") ||
        !strcmp(key, "headPos") || !strcmp(key, "torsoPos") ||
        !strcmp(key, "animID") || !strcmp(key, "animFrame") ||
        !strcmp(key, "animFrameAccelAssist") || !strcmp(key, "animAccel"))
        return luaL_error(L, "MarioBodyState.%s requires an engine capability absent from this port", key);
    return luaL_error(L, "MarioBodyState field '%s' is unsupported by this adapter", key);
}

static lua_Integer check_body_integer(lua_State *L, int index, lua_Integer minimum,
                                      lua_Integer maximum, const char *field) {
    lua_Integer value = luaL_checkinteger(L, index);
    if (value < minimum || value > maximum)
        luaL_error(L, "MarioBodyState.%s must be in range %lld..%lld", field,
                   (long long)minimum, (long long)maximum);
    return value;
}

static int mario_body_state_newindex(lua_State *L) {
    struct MarioBodyState *body = checked_mario_body_state(L, 1);
    const char *key = luaL_checkstring(L, 2);
#define SET_BODY(name, field, type, minimum, maximum) \
    if (!strcmp(key, name)) { \
        body->field = (type)check_body_integer(L, 3, minimum, maximum, name); \
        return 0; \
    }
    SET_BODY("action", action, u32, 0, UINT32_MAX)
    SET_BODY("capState", capState, s8, INT8_MIN, INT8_MAX)
    SET_BODY("eyeState", eyeState, s8, INT8_MIN, INT8_MAX)
    SET_BODY("handState", handState, s8, INT8_MIN, INT8_MAX)
    SET_BODY("wingFlutter", wingFlutter, s8, INT8_MIN, INT8_MAX)
    SET_BODY("modelState", modelState, s16, INT16_MIN, INT16_MAX)
    SET_BODY("grabPos", grabPos, s8, INT8_MIN, INT8_MAX)
    SET_BODY("punchState", punchState, u8, 0, UINT8_MAX)
#undef SET_BODY
    if (!strcmp(key, "torsoAngle") || !strcmp(key, "headAngle") ||
        !strcmp(key, "heldObjLastPosition"))
        return luaL_error(L, "MarioBodyState.%s is read-only", key);
    if (!strcmp(key, "allowPartRotation") || !strcmp(key, "mirrorMario") ||
        !strcmp(key, "headPos") || !strcmp(key, "torsoPos") ||
        !strcmp(key, "animID") || !strcmp(key, "animFrame") ||
        !strcmp(key, "animFrameAccelAssist") || !strcmp(key, "animAccel"))
        return luaL_error(L, "MarioBodyState.%s requires an engine capability absent from this port", key);
    return luaL_error(L, "MarioBodyState field '%s' is unsupported by this adapter", key);
}

static struct PlayerCameraState *checked_player_camera_state(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.PlayerCameraState");
    struct MarioState *owner = (struct MarioState *)ud->pointer;
    if (!owner || owner != current_mario())
        luaL_error(L, "stale PlayerCameraState reference");
    if (!owner->statusForCamera)
        luaL_error(L, "PlayerCameraState is unavailable");
    return owner->statusForCamera;
}

static int player_camera_state_index(lua_State *L) {
    struct PlayerCameraState *state = checked_player_camera_state(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "action")) lua_pushinteger(L, state->action);
    else if (!strcmp(key, "pos")) push_direct_vector(L, true, state->pos);
    else if (!strcmp(key, "faceAngle")) push_direct_vector(L, false, state->faceAngle);
    else if (!strcmp(key, "headRotation")) push_direct_vector(L, false, state->headRotation);
    else if (!strcmp(key, "unused")) lua_pushinteger(L, state->unused);
    else if (!strcmp(key, "cameraEvent")) lua_pushinteger(L, state->cameraEvent);
    else if (!strcmp(key, "usedObj")) {
        if (state->usedObj) push_object(L, state->usedObj); else lua_pushnil(L);
    } else return luaL_error(L, "PlayerCameraState field '%s' is unsupported by this adapter", key);
    return 1;
}

static int player_camera_state_newindex(lua_State *L) {
    struct PlayerCameraState *state = checked_player_camera_state(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "action")) {
        state->action = (u32)check_body_integer(L, 3, 0, UINT32_MAX, key);
        return 0;
    }
    if (!strcmp(key, "unused") || !strcmp(key, "cameraEvent")) {
        s16 value = (s16)check_body_integer(L, 3, INT16_MIN, INT16_MAX, key);
        if (!strcmp(key, "unused")) state->unused = value;
        else state->cameraEvent = value;
        return 0;
    }
    if (!strcmp(key, "usedObj")) {
        if (lua_isnil(L, 3)) state->usedObj = NULL;
        else state->usedObj = checked_object(L, 3);
        return 0;
    }
    if (!strcmp(key, "pos") || !strcmp(key, "faceAngle") || !strcmp(key, "headRotation"))
        return luaL_error(L, "PlayerCameraState.%s is a vector; mutate its x/y/z components", key);
    return luaL_error(L, "PlayerCameraState field '%s' is unsupported by this adapter", key);
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
    else if (!strcmp(key, "extStickX")) lua_pushinteger(L, m->controller->extStickX);
    else if (!strcmp(key, "extStickY")) lua_pushinteger(L, m->controller->extStickY);
    else if (!strcmp(key, "stickX")) lua_pushnumber(L, m->controller->stickX);
    else if (!strcmp(key, "stickY")) lua_pushnumber(L, m->controller->stickY);
    else if (!strcmp(key, "stickMag")) lua_pushnumber(L, m->controller->stickMag);
    else return luaL_error(L, "Controller field '%s' is unsupported by this adapter", key);
    return 1;
}

static int controller_newindex(lua_State *L) {
    (void)luaL_checkudata(L, 1, "sm64.mod.Controller");
    struct MarioState *m = current_mario();
    const char *key = luaL_checkstring(L, 2);
    if (!m || !m->controller) return luaL_error(L, "Controller is unavailable for the active Mario state");
    /* CoopDX mods consume or synthesize local controller input by changing
     * these fields. Keep the surface bounded to data fields; controller
     * pointers, port identity, and OS status remain owned by the engine. */
    if (!strcmp(key, "buttonDown") || !strcmp(key, "buttonPressed")) {
        u16 value = (u16)check_body_integer(L, 3, 0, UINT16_MAX, key);
        if (!strcmp(key, "buttonDown")) m->controller->buttonDown = value;
        else m->controller->buttonPressed = value;
        return 0;
    }
    if (!strcmp(key, "rawStickX") || !strcmp(key, "rawStickY") ||
        !strcmp(key, "extStickX") || !strcmp(key, "extStickY")) {
        s16 value = (s16)check_body_integer(L, 3, INT16_MIN, INT16_MAX, key);
        if (!strcmp(key, "rawStickX")) m->controller->rawStickX = value;
        else if (!strcmp(key, "rawStickY")) m->controller->rawStickY = value;
        else if (!strcmp(key, "extStickX")) m->controller->extStickX = value;
        else m->controller->extStickY = value;
        return 0;
    }
    if (!strcmp(key, "stickX") || !strcmp(key, "stickY") || !strcmp(key, "stickMag")) {
        lua_Number number = luaL_checknumber(L, 3);
        if (!isfinite((double)number)) return luaL_error(L, "Controller.%s must be finite", key);
        float value = (float)number;
        if (!strcmp(key, "stickMag")) {
            if (value < 0.0f || value > 64.0f) return luaL_error(L, "Controller.stickMag must be in [0, 64]");
            m->controller->stickMag = value;
        } else {
            if (value < -64.0f || value > 64.0f) return luaL_error(L, "Controller.%s must be in [-64, 64]", key);
            if (!strcmp(key, "stickX")) m->controller->stickX = value;
            else m->controller->stickY = value;
        }
        return 0;
    }
    return luaL_error(L, "Controller field '%s' is unsupported by this adapter", key);
}

static bool sCameraConfigUnavailableWarned;
static int push_camera_config_flag(lua_State *L, bool value) {
    lua_pushboolean(L, value);
#ifndef BETTERCAMERA
    if (!sCameraConfigUnavailableWarned && sCurrentMod) {
        sCameraConfigUnavailableWarned = true;
        mod_log("warning", sCurrentMod->name,
                "CoopDX camera config query reflects disabled native camera options because BETTERCAMERA is not compiled");
    }
#endif
    return 1;
}

static int l_camera_config_is_free_cam_enabled(lua_State *L) {
#ifdef BETTERCAMERA
    return push_camera_config_flag(L, configEnableCamera);
#else
    return push_camera_config_flag(L, false);
#endif
}

static int l_camera_config_is_x_inverted(lua_State *L) {
#ifdef BETTERCAMERA
    return push_camera_config_flag(L, configCameraInvertX);
#else
    return push_camera_config_flag(L, false);
#endif
}

static int l_camera_config_is_y_inverted(lua_State *L) {
#ifdef BETTERCAMERA
    return push_camera_config_flag(L, configCameraInvertY);
#else
    return push_camera_config_flag(L, false);
#endif
}

static struct Object *checked_object(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.Object");
    if (!object_reference_valid(ud)) luaL_error(L, "stale Object reference");
    return ud->object;
}

static LuaUserdata *checked_object_proxy(lua_State *L, int index, LuaUserdataKind kind, const char *metatable) {
    LuaUserdata *ud = luaL_checkudata(L, index, metatable);
    bool valid = ud->kind == kind;
    if (valid && kind == LUA_UD_GRAPH_NODE) {
        valid = ud->graph_node != NULL && (!ud->object || object_reference_valid(ud));
    } else if (valid && kind == LUA_UD_GRAPH_NODE_OBJECT) {
        valid = ud->graph_object != NULL && ud->shadow_position != NULL &&
                ud->disable_automatic_shadow_position != NULL;
    } else {
        valid = valid && object_reference_valid(ud);
    }
    if (!valid) luaL_error(L, "stale or invalid native graph reference");
    return ud;
}

static bool object_number_get(struct Object *o, const char *key, lua_State *L) {
#define OGET_NUM(name, field) if (!strcmp(key, name)) { lua_pushnumber(L, o->field); return true; }
#define OGET_INT(name, field) if (!strcmp(key, name)) { lua_pushinteger(L, o->field); return true; }
    OGET_NUM("oPosX", oPosX) OGET_NUM("oPosY", oPosY) OGET_NUM("oPosZ", oPosZ)
    OGET_NUM("oHomeX", oHomeX) OGET_NUM("oHomeY", oHomeY) OGET_NUM("oHomeZ", oHomeZ)
    OGET_NUM("oVelX", oVelX) OGET_NUM("oVelY", oVelY) OGET_NUM("oVelZ", oVelZ)
    OGET_NUM("oForwardVel", oForwardVel) OGET_NUM("oDrawingDistance", oDrawingDistance)
    OGET_NUM("hitboxRadius", hitboxRadius) OGET_NUM("hitboxHeight", hitboxHeight)
    OGET_NUM("hitboxDownOffset", hitboxDownOffset)
    OGET_NUM("hurtboxRadius", hurtboxRadius) OGET_NUM("hurtboxHeight", hurtboxHeight)
    OGET_INT("oAction", oAction) OGET_INT("oSubAction", oSubAction) OGET_INT("oTimer", oTimer)
    OGET_INT("oDamageOrCoinValue", oDamageOrCoinValue)
    OGET_INT("oIntangibleTimer", oIntangibleTimer) OGET_INT("oInteractStatus", oInteractStatus)
    OGET_INT("oFlags", oFlags) OGET_INT("oUnk94", oUnk94) OGET_INT("oInteractType", oInteractType)
    OGET_INT("oInteractionSubtype", oInteractionSubtype)
    OGET_INT("oBehParams", oBehParams) OGET_INT("oMoveAnglePitch", oMoveAnglePitch)
    OGET_INT("oMoveAngleYaw", oMoveAngleYaw) OGET_INT("oMoveAngleRoll", oMoveAngleRoll)
    OGET_INT("oFaceAnglePitch", oFaceAnglePitch) OGET_INT("oFaceAngleYaw", oFaceAngleYaw)
    OGET_INT("oFaceAngleRoll", oFaceAngleRoll) OGET_INT("activeFlags", activeFlags)
#undef OGET_NUM
#undef OGET_INT
    return false;
}

static bool object_number_set(struct Object *o, const char *key, lua_State *L, int i) {
#define OSET_NUM(name, field) if (!strcmp(key, name)) { o->field = (float)luaL_checknumber(L, i); return true; }
#define OSET_INT(name, field) if (!strcmp(key, name)) { o->field = (int32_t)luaL_checkinteger(L, i); return true; }
    OSET_NUM("oPosX", oPosX) OSET_NUM("oPosY", oPosY) OSET_NUM("oPosZ", oPosZ)
    OSET_NUM("oHomeX", oHomeX) OSET_NUM("oHomeY", oHomeY) OSET_NUM("oHomeZ", oHomeZ)
    OSET_NUM("oVelX", oVelX) OSET_NUM("oVelY", oVelY) OSET_NUM("oVelZ", oVelZ)
    OSET_NUM("oForwardVel", oForwardVel) OSET_NUM("oDrawingDistance", oDrawingDistance)
    OSET_NUM("hitboxRadius", hitboxRadius) OSET_NUM("hitboxHeight", hitboxHeight)
    OSET_NUM("hitboxDownOffset", hitboxDownOffset)
    OSET_NUM("hurtboxRadius", hurtboxRadius) OSET_NUM("hurtboxHeight", hurtboxHeight)
    OSET_INT("oAction", oAction) OSET_INT("oSubAction", oSubAction) OSET_INT("oTimer", oTimer)
    OSET_INT("oDamageOrCoinValue", oDamageOrCoinValue)
    OSET_INT("oIntangibleTimer", oIntangibleTimer) OSET_INT("oInteractStatus", oInteractStatus)
    OSET_INT("oFlags", oFlags) OSET_INT("oUnk94", oUnk94) OSET_INT("oInteractType", oInteractType)
    OSET_INT("oInteractionSubtype", oInteractionSubtype)
    OSET_INT("oBehParams", oBehParams) OSET_INT("oMoveAnglePitch", oMoveAnglePitch)
    OSET_INT("oMoveAngleYaw", oMoveAngleYaw) OSET_INT("oMoveAngleRoll", oMoveAngleRoll)
    OSET_INT("oFaceAnglePitch", oFaceAnglePitch) OSET_INT("oFaceAngleYaw", oFaceAngleYaw)
    OSET_INT("oFaceAngleRoll", oFaceAngleRoll) OSET_INT("activeFlags", activeFlags)
#undef OSET_NUM
#undef OSET_INT
    return false;
}

static int object_equal(lua_State *L) {
    const LuaUserdata *a = luaL_testudata(L, 1, "sm64.mod.Object");
    const LuaUserdata *b = luaL_testudata(L, 2, "sm64.mod.Object");
    lua_pushboolean(L, a && b && a->object == b->object && a->token == b->token);
    return 1;
}

static int pointer_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.Pointer");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "_pointer")) {
        if (ud->field != MOD_POINTER_BEHAVIOR)
            return luaL_error(L, "raw collision pointers are not exposed to Lua");
        lua_pushinteger(L, (lua_Integer)ud->pointer);
        return 1;
    }
    return 0;
}

static int pointer_equal(lua_State *L) {
    const LuaUserdata *a = luaL_testudata(L, 1, "sm64.mod.Pointer");
    const LuaUserdata *b = luaL_testudata(L, 2, "sm64.mod.Pointer");
    lua_pushboolean(L, a && b && a->field == b->field && a->pointer == b->pointer);
    return 1;
}

static int object_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.Object");
    if (!object_reference_valid(ud)) { lua_pushnil(L); return 1; }
    struct Object *o = ud->object;
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "header")) push_object_proxy(L, LUA_UD_OBJECT_HEADER, o, ud->token);
    else if (!strcmp(key, "hookRender")) lua_pushinteger(L, object_render_hook_id(o));
    else if (!strcmp(key, "globalPlayerIndex")) lua_pushinteger(L, 0);
    else if (!strcmp(key, "parentObj") && o->parentObj) push_object(L, o->parentObj);
    else if (!strcmp(key, "prevObj") && o->prevObj) push_object(L, o->prevObj);
    else if (!strcmp(key, "behavior")) push_behavior_pointer(L, object_behavior_script(o));
    else if (!strcmp(key, "collisionData")) push_collision_pointer(L, o->collisionData);
    else if (!strcmp(key, "oFaceAnglePitch")) lua_pushinteger(L, o->oFaceAnglePitch);
    else if (!strcmp(key, "oFaceAngleRoll")) lua_pushinteger(L, o->oFaceAngleRoll);
    else if (!object_number_get(o, key, L)) return luaL_error(L, "Object field '%s' is unsupported by this adapter", key);
    return 1;
}

static int object_newindex(lua_State *L) {
    struct Object *o = checked_object(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "hookRender")) {
        lua_Integer value = lua_isboolean(L, 3) ? (lua_toboolean(L, 3) ? 1 : 0) : luaL_checkinteger(L, 3);
        set_object_render_hook(L, o, value);
        return 0;
    }
    if (!strcmp(key, "globalPlayerIndex")) {
        lua_Integer value = luaL_checkinteger(L, 3);
        if (value != 0) return luaL_error(L, "nonzero Object.globalPlayerIndex requires multiplayer player ownership, which is unavailable in single-player mode");
        return 0;
    }
    if (!strcmp(key, "collisionData")) {
        if (lua_isnil(L, 3)) {
            o->collisionData = NULL;
            return 0;
        }
        LuaUserdata *collision = luaL_testudata(L, 3, "sm64.mod.Pointer");
        if (!collision || collision->kind != LUA_UD_POINTER ||
            collision->field != MOD_POINTER_COLLISION || !collision->pointer)
            return luaL_argerror(L, 3, "collisionData expects a Collision returned by smlua_collision_util_get or nil");
        o->collisionData = (Collision *)collision->pointer;
        return 0;
    }
    if (!strcmp(key, "oFaceAnglePitch")) o->oFaceAnglePitch = (s16)luaL_checkinteger(L, 3);
    else if (!strcmp(key, "oFaceAngleRoll")) o->oFaceAngleRoll = (s16)luaL_checkinteger(L, 3);
    else if (!object_number_set(o, key, L, 3)) return luaL_error(L, "Object field '%s' is read-only or unsupported", key);
    return 0;
}

static int object_header_index(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_OBJECT_HEADER, "sm64.mod.ObjectHeader");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "gfx")) push_object_proxy(L, LUA_UD_OBJECT_GFX, ud->object, ud->token);
    else return luaL_error(L, "ObjectHeader field '%s' is unsupported by this adapter", key);
    return 1;
}

static int object_gfx_index(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_OBJECT_GFX, "sm64.mod.ObjectGfx");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "node")) push_object_proxy(L, LUA_UD_GRAPH_NODE, ud->object, ud->token);
    else if (!strcmp(key, "animInfo")) push_object_proxy(L, LUA_UD_ANIMATION_INFO, ud->object, ud->token);
    else if (!strcmp(key, "cameraToObject")) push_object_camera_vector(L, ud->object, ud->token);
    else if (!strcmp(key, "shadowPos")) push_object_shadow_vector(L, ud->object, ud->token);
    else if (!strcmp(key, "disableAutomaticShadowPos")) {
        ObjectReference *ref = object_reference(ud->object, false);
        lua_pushboolean(L, ref && ref->disable_automatic_shadow_position);
    }
    else if (!strcmp(key, "pos")) push_direct_vector(L, true, ud->object->header.gfx.pos);
    else if (!strcmp(key, "scale")) push_direct_vector(L, true, ud->object->header.gfx.scale);
    else if (!strcmp(key, "angle")) push_direct_vector(L, false, ud->object->header.gfx.angle);
    else if (!strcmp(key, "gfxOffset")) {
        if (sCurrentMod && !sCurrentMod->gfx_offset_warned) {
            sCurrentMod->gfx_offset_warned = true;
            mod_log("warning", sCurrentMod->name,
                    "Object.gfxOffset is not present in this engine; exposing its inactive zero-offset state");
        }
        push_gfx_offset(L, ud->object, ud->token);
    }
    else return luaL_error(L, "ObjectGfx field '%s' is unsupported by this adapter", key);
    return 1;
}

static int object_gfx_newindex(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_OBJECT_GFX, "sm64.mod.ObjectGfx");
    const char *key = luaL_checkstring(L, 2);
    ObjectReference *ref = object_reference(ud->object, false);
    if (!ref) return luaL_error(L, "ObjectGfx reference has no native object state");
    if (!strcmp(key, "disableAutomaticShadowPos")) {
        if (!lua_isboolean(L, 3)) return luaL_argerror(L, 3, "disableAutomaticShadowPos must be a boolean");
        ref->disable_automatic_shadow_position = lua_toboolean(L, 3) != 0;
        return 0;
    }
    return luaL_error(L, "ObjectGfx.%s is read-only or unsupported", key);
}

static struct GraphNodeObject_sub *checked_animation_info(lua_State *L, int index) {
    LuaUserdata *ud = checked_object_proxy(L, index, LUA_UD_ANIMATION_INFO, "sm64.mod.AnimationInfo");
    return &ud->object->header.gfx.unk38;
}

static int animation_info_index(lua_State *L) {
    struct GraphNodeObject_sub *info = checked_animation_info(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "curAnim")) push_animation(L, info->curAnim);
    else if (!strcmp(key, "animID")) lua_pushinteger(L, info->animID);
    else if (!strcmp(key, "animYTrans")) lua_pushinteger(L, info->animYTrans);
    else if (!strcmp(key, "animFrame")) lua_pushinteger(L, info->animFrame);
    else if (!strcmp(key, "animTimer")) lua_pushinteger(L, info->animTimer);
    else if (!strcmp(key, "animFrameAccelAssist")) lua_pushinteger(L, info->animFrameAccelAssist);
    else if (!strcmp(key, "animAccel")) lua_pushinteger(L, info->animAccel);
    else return luaL_error(L, "AnimationInfo.%s is not represented by this engine's native animation state", key);
    return 1;
}

static int animation_info_newindex(lua_State *L) {
    struct GraphNodeObject_sub *info = checked_animation_info(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "curAnim")) {
        if (lua_isnil(L, 3)) { info->curAnim = NULL; return 0; }
        LuaUserdata *animation = luaL_checkudata(L, 3, "sm64.mod.Animation");
        if (animation->kind != LUA_UD_ANIMATION || !animation->pointer)
            return luaL_argerror(L, 3, "curAnim expects an Animation or nil");
        struct Animation *value = (struct Animation *)animation->pointer;
        if (animation->field && !find_animation_pointer(value))
            return luaL_argerror(L, 3, "curAnim refers to a mod animation that has been unloaded");
        info->curAnim = value;
        return 0;
    }
    lua_Integer value = luaL_checkinteger(L, 3);
    if (!strcmp(key, "animID") || !strcmp(key, "animYTrans") || !strcmp(key, "animFrame")) {
        if (value < INT16_MIN || value > INT16_MAX) return luaL_argerror(L, 3, "value must fit in s16");
        if (!strcmp(key, "animID")) info->animID = (s16)value;
        else if (!strcmp(key, "animYTrans")) info->animYTrans = (s16)value;
        else info->animFrame = (s16)value;
    } else if (!strcmp(key, "animTimer")) {
        if (value < 0 || value > UINT16_MAX) return luaL_argerror(L, 3, "value must fit in u16");
        info->animTimer = (u16)value;
    } else if (!strcmp(key, "animFrameAccelAssist")) {
        if (value < INT32_MIN || value > INT32_MAX) return luaL_argerror(L, 3, "value must fit in s32");
        info->animFrameAccelAssist = (s32)value;
    } else if (!strcmp(key, "animAccel")) {
        if (value < INT32_MIN || value > INT32_MAX) return luaL_argerror(L, 3, "value must fit in s32");
        info->animAccel = (s32)value;
    } else return luaL_error(L, "AnimationInfo.%s is not represented by this engine's native animation state", key);
    return 0;
}

static int animation_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.Animation");
    if (ud->kind != LUA_UD_ANIMATION || !ud->pointer)
        return luaL_error(L, "invalid Animation reference");
    struct Animation *animation = (struct Animation *)ud->pointer;
    ModAnimation *registered = ud->field ? find_animation_pointer(animation) : NULL;
    if (ud->field && !registered) return luaL_error(L, "stale mod Animation reference");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "flags")) lua_pushinteger(L, animation->flags);
    else if (!strcmp(key, "animYTransDivisor")) lua_pushinteger(L, animation->unk02);
    else if (!strcmp(key, "startFrame")) lua_pushinteger(L, animation->unk04);
    else if (!strcmp(key, "loopStart")) lua_pushinteger(L, animation->unk06);
    else if (!strcmp(key, "loopEnd")) lua_pushinteger(L, animation->unk08);
    else if (!strcmp(key, "length")) lua_pushinteger(L, animation->length);
    else if (!strcmp(key, "valuesLength")) lua_pushinteger(L, registered ? registered->values_length : 0);
    else if (!strcmp(key, "indexLength")) lua_pushinteger(L, registered ? registered->index_length : 0);
    else return luaL_error(L, "Animation.%s requires pointer or decoder support not available in this engine", key);
    return 1;
}

static void push_native_character(lua_State *L) {
    LuaUserdata *ud = lua_newuserdata(L, sizeof(*ud));
    *ud = (LuaUserdata){ .kind = LUA_UD_CHARACTER, .field = 0 };
    luaL_getmetatable(L, "sm64.mod.Character"); lua_setmetatable(L, -2);
}

static int character_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.Character");
    if (ud->kind != LUA_UD_CHARACTER || ud->field != 0)
        return luaL_error(L, "invalid Character reference");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "type")) lua_pushinteger(L, 0); /* CoopDX CT_MARIO: the native actor remains Mario. */
    else if (!strcmp(key, "name")) lua_pushliteral(L, "Mario");
    else if (!strcmp(key, "modelId")) lua_pushinteger(L, MODEL_MARIO);
    else return luaL_error(L, "Character.%s requires a character-selection/resource system not present in this engine", key);
    return 1;
}

static int graph_node_object_index(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_GRAPH_NODE_OBJECT, "sm64.mod.GraphNodeObject");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "node")) push_graph_node(L, &ud->graph_object->node);
    else if (!strcmp(key, "pos")) push_direct_vector(L, true, ud->graph_object->pos);
    else if (!strcmp(key, "angle")) push_direct_vector(L, false, ud->graph_object->angle);
    else if (!strcmp(key, "scale")) push_direct_vector(L, true, ud->graph_object->scale);
    else if (!strcmp(key, "shadowPos")) push_direct_vector(L, true, ud->shadow_position);
    else if (!strcmp(key, "disableAutomaticShadowPos"))
        lua_pushboolean(L, *ud->disable_automatic_shadow_position);
    else return luaL_error(L, "GraphNodeObject field '%s' needs an engine adapter", key);
    return 1;
}

static int graph_node_object_newindex(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_GRAPH_NODE_OBJECT, "sm64.mod.GraphNodeObject");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "disableAutomaticShadowPos")) {
        if (!lua_isboolean(L, 3)) return luaL_argerror(L, 3, "disableAutomaticShadowPos must be a boolean");
        *ud->disable_automatic_shadow_position = lua_toboolean(L, 3) != 0;
        return 0;
    }
    return luaL_error(L, "GraphNodeObject field '%s' is read-only or unsupported", key);
}

static int graph_node_index(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_GRAPH_NODE, "sm64.mod.GraphNode");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "flags")) lua_pushinteger(L, (uint16_t)ud->graph_node->flags);
    else return luaL_error(L, "GraphNode field '%s' is unsupported by this adapter", key);
    return 1;
}

static int graph_node_newindex(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_GRAPH_NODE, "sm64.mod.GraphNode");
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "flags")) return luaL_error(L, "GraphNode field '%s' is read-only or unsupported", key);
    lua_Integer flags = luaL_checkinteger(L, 3);
    if (flags < 0 || flags > UINT16_MAX) return luaL_argerror(L, 3, "graph flags must fit the engine's 16-bit field");
    s16 *native_flags = &ud->graph_node->flags;
    bool was_visible = (*native_flags & GRAPH_RENDER_ACTIVE) != 0;
    bool visible = (flags & GRAPH_RENDER_ACTIVE) != 0;
    if (sCurrentMod && was_visible != visible) {
        ObjectReference *ref = ud->object ? object_reference(ud->object, false) : NULL;
        if (ref) {
            if (!ref->visibility_owner || was_visible != ref->written_visible)
                ref->original_visible = was_visible;
            ref->visibility_owner = visible == ref->original_visible ? NULL : sCurrentMod;
            ref->written_visible = visible;
        }
    }
    *native_flags = (s16)(uint16_t)flags;
    return 0;
}

static int gfx_offset_index(lua_State *L) {
    LuaUserdata *ud = checked_object_proxy(L, 1, LUA_UD_GFX_OFFSET, "sm64.mod.GfxOffset");
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "active")) lua_pushboolean(L, false);
    else if (!strcmp(key, "x") || !strcmp(key, "y") || !strcmp(key, "z")) lua_pushnumber(L, 0.0);
    else return luaL_error(L, "GfxOffset field '%s' is unsupported", key);
    return 1;
}

static int gfx_offset_newindex(lua_State *L) {
    return luaL_error(L, "GfxOffset has no writable native storage in this engine");
}

static int surface_index(lua_State *L) {
    LuaUserdata *ud = luaL_checkudata(L, 1, "sm64.mod.Surface");
    const char *key = luaL_checkstring(L, 2);
    const struct Surface *surface = &ud->surface;
    if (!strcmp(key, "type")) lua_pushinteger(L, surface->type);
    else if (!strcmp(key, "force")) lua_pushinteger(L, surface->force);
    else if (!strcmp(key, "flags")) lua_pushinteger(L, surface->flags);
    else if (!strcmp(key, "room")) lua_pushinteger(L, surface->room);
    else if (!strcmp(key, "lowerY")) lua_pushinteger(L, surface->lowerY);
    else if (!strcmp(key, "upperY")) lua_pushinteger(L, surface->upperY);
    else if (!strcmp(key, "normal")) {
        lua_newtable(L);
        lua_pushnumber(L, surface->normal.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, surface->normal.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, surface->normal.z); lua_setfield(L, -2, "z");
    } else return luaL_error(L, "Surface field '%s' is unsupported by this adapter", key);
    return 1;
}

static int surface_newindex(lua_State *L) {
    return luaL_error(L, "Surface snapshots are read-only");
}

static int l_hook_event(lua_State *L) {
    int event = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (event < 0 || event >= MOD_EVENT_COUNT) return luaL_error(L, "unknown CoopDX hook event id %d", event);
    if (!sCurrentMod) return luaL_error(L, "hook_event must be called while loading a mod");
    if (event == HOOK_ON_PACKET_RECEIVE) {
        if (!sCurrentMod->packet_hook_warned) {
            sCurrentMod->packet_hook_warned = true;
            mod_log("warning", sCurrentMod->name,
                    "HOOK_ON_PACKET_RECEIVE (22) is inactive: multiplayer packet transport is unavailable");
        }
        return 0;
    }
    if (event == HOOK_ALLOW_PVP_ATTACK || event == HOOK_ON_PVP_ATTACK ||
        event == HOOK_ON_PLAYER_CONNECTED || event == HOOK_ON_PLAYER_DISCONNECTED) {
        if (sCurrentMod && !sCurrentMod->packet_hook_warned) {
            sCurrentMod->packet_hook_warned = true;
            mod_logf("warning", sCurrentMod->name,
                     "CoopDX hook event %d is inactive: multiplayer players and lobby transport are unavailable in this single-player runtime",
                     event);
        }
        return 0;
    }
    if (event != HOOK_UPDATE && event != HOOK_MARIO_UPDATE && event != HOOK_BEFORE_MARIO_UPDATE
        && event != HOOK_ON_SET_MARIO_ACTION && event != HOOK_BEFORE_PHYS_STEP && event != HOOK_ON_HUD_RENDER
        && event != HOOK_ON_HUD_RENDER_BEHIND
        && event != HOOK_ALLOW_INTERACT && event != HOOK_ON_OBJECT_RENDER && event != HOOK_OBJECT_SET_MODEL
        && event != HOOK_ON_INTERACT && event != HOOK_ON_LEVEL_INIT && event != HOOK_ON_WARP
        && event != HOOK_USE_ACT_SELECT
        && event != HOOK_ON_SET_CAMERA_MODE && event != HOOK_ON_CHANGE_CAMERA_ANGLE
        && event != HOOK_ON_DEATH && event != HOOK_ON_PAUSE_EXIT
        && event != HOOK_ALLOW_HAZARD_SURFACE && event != 30
        && event != HOOK_ON_EXIT && event != HOOK_MIRROR_MARIO_RENDER)
        return luaL_error(L, "CoopDX hook event %d is not supported by the current engine adapter", event);
    lua_pushvalue(L, 2);
    if (!add_hook(sCurrentMod, event, luaL_ref(L, LUA_REGISTRYINDEX)))
        return luaL_error(L, "out of memory registering hook_event callback");
    return 0;
}

static int l_hook_on_sync_table_change(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_isnoneornil(L, 2)) return luaL_argerror(L, 2, "sync table field cannot be nil");
    luaL_checkany(L, 3);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    if (!sCurrentMod) return luaL_error(L, "hook_on_sync_table_change must be called while loading a mod");
    if (!lua_getmetatable(L, 1)) return luaL_argerror(L, 1, "expected a CoopDX SyncTable");
    lua_getfield(L, -1, "__sm64_sync_table");
    bool is_sync_table = lua_toboolean(L, -1);
    lua_pop(L, 2);
    if (!is_sync_table) return luaL_argerror(L, 1, "expected a CoopDX SyncTable");
    if (lua_type(L, 2) == LUA_TNUMBER && isnan(lua_tonumber(L, 2)))
        return luaL_argerror(L, 2, "sync table field cannot be NaN");
    if (sSyncHookCount >= MOD_MAX_SYNC_HOOKS)
        return luaL_error(L, "sync-table hook capacity exhausted");

    ModSyncHook *hook = calloc(1, sizeof(*hook));
    if (!hook) return luaL_error(L, "out of memory registering sync-table hook");
    hook->owner = sCurrentMod;
    hook->file = strdup(sCurrentFile ? sCurrentFile : "<unknown>");
    if (!hook->file) { free(hook); return luaL_error(L, "out of memory registering sync-table hook"); }
    lua_pushvalue(L, 1); hook->table_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 2); hook->field_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 3); hook->tag_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 4); hook->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    hook->enabled = true;
    hook->next_global = sSyncHooks; sSyncHooks = hook;
    hook->next_owned = sCurrentMod->sync_hooks; sCurrentMod->sync_hooks = hook;
    ++sSyncHookCount;
    return 0;
}

static int l_allocate_mario_action(lua_State *L) {
    uint32_t flags = (uint32_t)luaL_optinteger(L, 1, 0);
    if (!sCurrentMod) return luaL_error(L, "allocate_mario_action must be called while loading a mod");
    unsigned group = flags & ACT_GROUP_MASK;
    unsigned slot = group;
    while (slot < group + MOD_ACTIONS_PER_GROUP && sActionSlots[slot]) ++slot;
    if (slot == group + MOD_ACTIONS_PER_GROUP)
        return luaL_error(L, "custom Mario action capacity exhausted for group %d", (int)group);
    ModCustomActionId *allocated = calloc(1, sizeof(*allocated));
    if (!allocated) return luaL_error(L, "out of memory allocating a custom Mario action");
    sActionSlots[slot] = true;
    allocated->owner = sCurrentMod;
    allocated->slot = slot;
    allocated->id = (flags & ~ACT_ID_MASK) | ACT_FLAG_CUSTOM_ACTION | slot;
    allocated->next_owned = sCurrentMod->custom_action_ids;
    sCurrentMod->custom_action_ids = allocated;
    uint32_t id = allocated->id;
    lua_pushinteger(L, id);
    return 1;
}

static int l_hook_mario_action(lua_State *L) {
    lua_Integer action_value = luaL_checkinteger(L, 1);
    if (action_value <= 0 || (uint64_t)action_value > UINT32_MAX)
        return luaL_argerror(L, 1, "action ID must be in 1..UINT32_MAX");
    int argument_count = lua_gettop(L);
    if (argument_count < 2 || argument_count > 3)
        return luaL_error(L, "hook_mario_action expects (action, callback[, interactionType]) or (action, hooks[, interactionType])");
    if (!sCurrentMod) return luaL_error(L, "hook_mario_action must be called while loading a mod");
    uint32_t interaction_type = 0;
    if (argument_count == 3) {
        lua_Integer interaction_value = luaL_checkinteger(L, 3);
        if (interaction_value < 0 || (uint64_t)interaction_value > UINT32_MAX)
            return luaL_argerror(L, 3, "interactionType must be in 0..UINT32_MAX");
        interaction_type = (uint32_t)interaction_value;
        /* CoopDX reserves bit 31 for player-versus-player interaction. This
         * port has no multiplayer player targets; keep the local action
         * hook, but don't feed an unknown interaction bit into SM64. */
        if (interaction_type & (UINT32_C(1) << 31)) {
            if (!sCurrentMod->player_interaction_warned) {
                sCurrentMod->player_interaction_warned = true;
                mod_log("warning", sCurrentMod->name,
                        "INTERACT_PLAYER was requested by a custom action; player-versus-player interactions are unavailable in single-player, so that interaction bit is ignored");
            }
            interaction_type &= ~(UINT32_C(1) << 31);
        }
    }

    int every = LUA_NOREF;
    int gravity = LUA_NOREF;
    if (lua_isfunction(L, 2)) {
        /* CoopDX keeps this legacy form as an every_frame callback. */
        lua_pushvalue(L, 2);
        every = luaL_ref(L, LUA_REGISTRYINDEX);
    } else if (lua_istable(L, 2)) {
        every = callback_ref_from_table(L, 2, "every_frame");
        gravity = callback_ref_from_table(L, 2, "gravity");
    } else {
        return luaL_argerror(L, 2, "expected a callback function or action-hook table");
    }
    if (every == LUA_NOREF && gravity == LUA_NOREF) return luaL_error(L, "action hook requires every_frame or gravity function");
    if (!add_action(sCurrentMod, (uint32_t)action_value, interaction_type, every, gravity))
        return luaL_error(L, "out of memory registering Mario action callbacks");
    return 0;
}

static void script_behavior_native(void);

static int l_hook_behavior(lua_State *L) {
    if (!sCurrentMod) return luaL_error(L, "hook_behavior must be called while loading a mod");
    if (!lua_isnil(L, 1)) {
        lua_Integer id = luaL_checkinteger(L, 1);
        if (id < 0 || (uint64_t)id > UINT32_MAX)
            return luaL_argerror(L, 1, "behavior ID must be in 0..UINT32_MAX or nil");
        if (find_behavior((uint32_t)id))
            return luaL_error(L, "hook_behavior on a custom behavior ID is not supported; hook its original registration instead");
        const BehaviorScript *script = native_behavior_from_id(id);
        if (!script) return behavior_id_error(L, id, "hook_behavior");
        lua_Integer list_value = luaL_checkinteger(L, 2);
        int native_list = get_object_list_from_behavior(script);
        if (list_value < 0 || list_value >= NUM_OBJ_LISTS || list_value != native_list)
            return luaL_argerror(L, 2, "object list must match the registered native behavior's object list");
        luaL_checktype(L, 3, LUA_TBOOLEAN);
        bool replace = lua_toboolean(L, 3) != 0;
        for (int index = 4; index <= 5; ++index) {
            if (!lua_isnoneornil(L, index) && !lua_isfunction(L, index))
                return luaL_argerror(L, index, "expected a function or nil");
        }
        if (lua_isnoneornil(L, 4) && lua_isnoneornil(L, 5))
            return luaL_error(L, "native behavior hook requires an init or loop function");
        if (!lua_isnoneornil(L, 6)) {
            size_t name_length = 0;
            const char *name = luaL_checklstring(L, 6, &name_length);
            if (name_length == 0 || name_length > 240 || memchr(name, '\0', name_length) != NULL)
                return luaL_argerror(L, 6, "behavior name must contain 1..240 bytes and no embedded NUL");
        }
        ModBehavior *hook = calloc(1, sizeof(*hook));
        if (!hook) return luaL_error(L, "out of memory registering native behavior hook");
        hook->file = strdup(sCurrentFile ? sCurrentFile : "main.lua");
        if (!hook->file) { free(hook); return luaL_error(L, "out of memory registering native behavior hook source"); }
        if (!lua_isnoneornil(L, 6)) {
            size_t name_length = 0;
            const char *name = lua_tolstring(L, 6, &name_length);
            hook->name = strndup(name, name_length);
            if (!hook->name) {
                free(hook->file);
                free(hook);
                return luaL_error(L, "out of memory registering native behavior hook name");
            }
        }
        hook->owner = sCurrentMod;
        hook->id = (uint32_t)id;
        hook->object_list = native_list;
        hook->enabled = true;
        hook->native_hook = true;
        hook->replace_behavior = replace;
        hook->init_ref = LUA_NOREF;
        hook->loop_ref = LUA_NOREF;
        if (lua_isfunction(L, 4)) { lua_pushvalue(L, 4); hook->init_ref = luaL_ref(L, LUA_REGISTRYINDEX); }
        if (lua_isfunction(L, 5)) { lua_pushvalue(L, 5); hook->loop_ref = luaL_ref(L, LUA_REGISTRYINDEX); }
        hook->next_global = sBehaviors;
        sBehaviors = hook;
        hook->next_owned = sCurrentMod->behaviors;
        sCurrentMod->behaviors = hook;
        lua_pushinteger(L, id);
        return 1;
    }
    lua_Integer list_value = luaL_checkinteger(L, 2);
    if (list_value < 0 || list_value >= NUM_OBJ_LISTS)
    {
        char message[128];
        snprintf(message, sizeof(message), "object list %lld is unsupported for script behaviors", (long long)list_value);
        return luaL_error(L, "%s", message);
    }
    int list = (int)list_value;
    (void)lua_toboolean(L, 3); /* replaceBehavior is meaningful only when replacing native behavior */
    luaL_checktype(L, 4, LUA_TFUNCTION);
    luaL_checktype(L, 5, LUA_TFUNCTION);
    if (list < 0 || list >= NUM_OBJ_LISTS || list == OBJ_LIST_PLAYER)
        return luaL_error(L, "object list %d is unsupported for script behaviors", list);
    ModBehavior *behavior = calloc(1, sizeof(*behavior));
    if (!behavior) return luaL_error(L, "out of memory registering behavior");
    behavior->file = strdup(sCurrentFile ? sCurrentFile : "main.lua");
    if (!behavior->file) { free(behavior); return luaL_error(L, "out of memory registering behavior source"); }
    if (!lua_isnoneornil(L, 6)) {
        size_t name_length = 0;
        const char *name = luaL_checklstring(L, 6, &name_length);
        if (name_length == 0 || name_length > 240 || memchr(name, '\0', name_length) != NULL) {
            free(behavior->file);
            free(behavior);
            return luaL_argerror(L, 6, "behavior name must contain 1..240 bytes and no embedded NUL");
        }
        behavior->name = strndup(name, name_length);
        if (!behavior->name) {
            free(behavior->file);
            free(behavior);
            return luaL_error(L, "out of memory registering behavior name");
        }
    }
    behavior->owner = sCurrentMod; behavior->id = MOD_DYNAMIC_ID_BASE | sNextBehaviorId++;
    behavior->object_list = list; behavior->enabled = true;
    behavior->script = calloc(5, sizeof(*behavior->script));
    if (!behavior->script) {
        free(behavior->file);
        free(behavior->name);
        free(behavior);
        return luaL_error(L, "out of memory allocating behavior script");
    }
    behavior->script_word_count = 5;
    behavior->script[0] = ((uintptr_t)list & 0xFFu) << 16;
    behavior->script[1] = (uintptr_t)0x08u << 24;
    behavior->script[2] = (uintptr_t)0x0Cu << 24;
    behavior->script[3] = (uintptr_t)script_behavior_native;
    behavior->script[4] = (uintptr_t)0x09u << 24;
    lua_pushvalue(L, 4); behavior->init_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 5); behavior->loop_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    behavior->next_global = sBehaviors; sBehaviors = behavior;
    behavior->next_owned = sCurrentMod->behaviors; sCurrentMod->behaviors = behavior;
    lua_pushinteger(L, behavior->id);
    return 1;
}

static ModBehavior *find_behavior(uint32_t id) {
    for (ModBehavior *b = sBehaviors; b; b = b->next_global)
        if (!b->native_hook && b->id == id && b->enabled) return b;
    return NULL;
}

static ModBehavior *find_behavior_by_script(const BehaviorScript *script) {
    for (ModBehavior *b = sBehaviors; b; b = b->next_global)
        if (b->enabled && (const BehaviorScript *)b->script == script) return b;
    return NULL;
}

static const BehaviorScript *native_behavior_from_id(lua_Integer id) {
    if (id < 0 || (uint64_t)id >= sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0])) return NULL;
    const NativeBehavior *behavior = &sNativeBehaviors[id];
    if (behavior->id != id) return NULL;
    return behavior->script;
}

static lua_Integer native_behavior_id_from_script(const BehaviorScript *script) {
    if (!script) return (lua_Integer)(sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0]));
    for (size_t i = 0; i < sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0]); ++i) {
        if (sNativeBehaviors[i].script == script) return sNativeBehaviors[i].id;
    }
    return (lua_Integer)(sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0]));
}

static const BehaviorScript *behavior_from_id(lua_State *L, int index, ModBehavior **custom_out) {
    lua_Integer id = luaL_checkinteger(L, index);
    ModBehavior *custom = id >= 0 && (uint64_t)id <= UINT32_MAX ? find_behavior((uint32_t)id) : NULL;
    if (custom_out) *custom_out = custom;
    if (custom) return (const BehaviorScript *)custom->script;
    return native_behavior_from_id(id);
}

static int behavior_id_error(lua_State *L, lua_Integer id, const char *api) {
    char message[384];
    if (id >= 0 && (uint64_t)id < sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0]))
        snprintf(message, sizeof(message), "%s: CoopDX behavior ID %lld (%s) is not implemented by this engine",
                 api, (long long)id, sNativeBehaviors[id].name);
    else if (id >= 0 && (uint64_t)id <= UINT32_MAX)
        snprintf(message, sizeof(message), "%s: unknown or inactive custom behavior ID %lld", api, (long long)id);
    else
        snprintf(message, sizeof(message), "%s: unknown behavior ID %lld", api, (long long)id);
    return luaL_error(L, "%s", message);
}

static int native_model_from_id(lua_State *L, lua_Integer id, const char *api) {
    for (DynosModel *dynamic = sDynosModels; dynamic; dynamic = dynamic->next_global)
        if ((uint64_t)id == dynamic->extended_id && gLoadedGraphNodes &&
            gLoadedGraphNodes[dynamic->native_slot] == dynamic->graph)
            return dynamic->native_slot;
    const size_t count = sizeof(sNativeModels) / sizeof(sNativeModels[0]);
    char message[384];
    if (id < 0 || (uint64_t)id >= count) {
        snprintf(message, sizeof(message), "%s: CoopDX model ID %lld is outside the supported built-in range; custom DynOS models are unavailable",
                 api, (long long)id);
        return luaL_error(L, "%s", message);
    }
    const NativeModel *model = &sNativeModels[id];
    if (model->id != id) {
        snprintf(message, sizeof(message), "%s: CoopDX model table is inconsistent at ID %lld", api, (long long)id);
        return luaL_error(L, "%s", message);
    }
    if (model->native_model < 0)
        return luaL_error(L, "%s: %s requires a model asset this engine does not provide; DynOS model decoding is unavailable",
                          api, model->name);
    if (model->native_model != MODEL_NONE &&
        (!gLoadedGraphNodes || !gLoadedGraphNodes[model->native_model]))
        return luaL_error(L, "%s: %s maps to native model %d, but that model is not loaded in the current level",
                          api, model->name, model->native_model);
    return model->native_model;
}

int mod_runtime_extended_model_id(int native_model) {
    for (DynosModel *dynamic = sDynosModels; dynamic; dynamic = dynamic->next_global)
        if (dynamic->native_slot == native_model) return (int)dynamic->extended_id;
    for (size_t i = 0; i < sizeof(sNativeModels) / sizeof(sNativeModels[0]); ++i)
        if (sNativeModels[i].native_model == native_model) return (int)sNativeModels[i].id;
    return 159; /* E_MODEL_ERROR_MODEL for a model without a CoopDX mapping. */
}

/* Actors are activated only when their binary resources link to native Gfx
 * and their root GeoLayout falls within the bounded engine-backed subset. */
static int l_smlua_model_util_get_id(lua_State *L) {
    size_t length = 0;
    const char *name = luaL_checklstring(L, 1, &length);
    if (!length || length > 240 || memchr(name, '\0', length) != NULL)
        return luaL_error(L, "smlua_model_util_get_id expects a non-empty model name of at most 240 bytes");

    char reason[400] = "no active DynOS geolayout matches this name";
    DynosModel *model = sCurrentMod ? load_dynos_model(sCurrentMod, name, reason, sizeof(reason)) : NULL;
    if (model) {
        lua_pushinteger(L, model->extended_id);
        return 1;
    }
    mod_logf("warning", sCurrentMod ? sCurrentMod->name : NULL,
             "smlua_model_util_get_id('%s'): %s; returning E_MODEL_ERROR_MODEL (159)",
             name, reason);
    lua_pushinteger(L, 159); /* E_MODEL_ERROR_MODEL from CoopDX v1.5.1. */
    return 1;
}

/* CoopDX first resolves native collision names, then DynOS-pack resources.
 * The native object collision arrays are compiled into this engine and are
 * safe to expose by name. Binary DynOS .col decoding is intentionally not
 * implied by this lookup and remains an explicit unsupported path. */
static int l_smlua_collision_util_get(lua_State *L) {
    size_t length = 0;
    const char *name = luaL_checklstring(L, 1, &length);
    if (!length || length > 240 || memchr(name, '\0', length) != NULL)
        return luaL_error(L, "smlua_collision_util_get expects a non-empty collision name of at most 240 bytes");

    for (size_t i = 0; i < sizeof(sNativeCollisions) / sizeof(sNativeCollisions[0]); ++i) {
        if (sNativeCollisions[i].data && strcmp(name, sNativeCollisions[i].name) == 0) {
            push_collision_pointer(L, sNativeCollisions[i].data);
            return 1;
        }
    }

    bool package_has_asset = false;
    char path[SYS_MAX_PATH];
    if (sCurrentMod && safe_relative_path(name)) {
        static const char *const roots[] = { "", "actors/", "collisions/" };
        for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
            int written = snprintf(path, sizeof(path), "%s/%s%s.col",
                                   sCurrentMod->path, roots[i], name);
            if (written > 0 && (size_t)written < sizeof(path) && fs_sys_file_exists(path)) {
                package_has_asset = true;
                break;
            }
        }
    }
    if (sCurrentMod) {
        mod_logf("warning", sCurrentMod->name,
                 "smlua_collision_util_get('%.*s') returned nil: %s; DynOS .col decoding and registration are unavailable",
                 (int)length, name, package_has_asset
                     ? "the package contains this collision asset"
                     : "no linked native actor collision with this name exists");
    }
    lua_pushnil(L);
    return 1;
}

static float checked_lua_finite_float(lua_State *L, int index, const char *name) {
    lua_Number value = luaL_checknumber(L, index);
    if (!isfinite((double)value) || value > FLT_MAX || value < -FLT_MAX)
        luaL_error(L, "%s must be a finite value representable by the engine", name);
    return (float)value;
}

static int object_list_from_behavior_id(lua_State *L, ModBehavior *custom, const BehaviorScript *script) {
    int list = custom ? custom->object_list : (int)get_object_list_from_behavior(script);
    if (!gObjectLists || list < 0 || list >= NUM_OBJ_LISTS)
        return luaL_error(L, "behavior ID does not map to a live engine object list");
    return list;
}

static struct Object *find_object_in_list(int list, struct ObjectNode *node,
                                          ModBehavior *custom, const BehaviorScript *script) {
    if (!gObjectLists || list < 0 || list >= NUM_OBJ_LISTS) return NULL;
    struct ObjectNode *head = &gObjectLists[list];
    unsigned scanned = 0;
    while (node != head && node && scanned++ < OBJECT_POOL_CAPACITY) {
        struct Object *object = (struct Object *)node;
        ModObject *entry = custom ? find_object(object) : NULL;
        bool matches = custom ? (entry && entry->behavior == custom)
                              : (script ? object->behavior == script : true);
        if (matches && object->activeFlags != ACTIVE_FLAG_DEACTIVATED) return object;
        node = node->next;
    }
    return NULL;
}

static int l_get_id_from_behavior(lua_State *L) {
    if (lua_isnil(L, 1)) {
        lua_pushinteger(L, (lua_Integer)(sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0])));
        return 1;
    }
    lua_Integer value;
    if (luaL_testudata(L, 1, "sm64.mod.Pointer")) {
        LuaUserdata *pointer = luaL_checkudata(L, 1, "sm64.mod.Pointer");
        if (pointer->kind != LUA_UD_POINTER || pointer->field != MOD_POINTER_BEHAVIOR)
            return luaL_argerror(L, 1, "expected a behavior pointer");
        value = (lua_Integer)pointer->pointer;
    } else {
        value = luaL_checkinteger(L, 1);
    }
    ModBehavior *custom = NULL;
    if (value > 0 && (uint64_t)value <= (uint64_t)UINTPTR_MAX) {
        custom = find_behavior_by_script((const BehaviorScript *)(uintptr_t)value);
        if (custom) { lua_pushinteger(L, (lua_Integer)custom->id); return 1; }
    }
    /* CoopDX returns id_bhv_max_count when the script is not in its table. */
    lua_pushinteger(L, native_behavior_id_from_script((const BehaviorScript *)(uintptr_t)value));
    return 1;
}

static int l_get_behavior_from_id(lua_State *L) {
    ModBehavior *custom = NULL;
    const BehaviorScript *script = behavior_from_id(L, 1, &custom);
    if (!script) {
        lua_Integer id = luaL_checkinteger(L, 1);
        bool should_warn = true;
        if (sCurrentMod && id >= 0 && id < MOD_NATIVE_BEHAVIOR_COUNT) {
            size_t word = (size_t)id / 64;
            uint64_t mask = UINT64_C(1) << ((unsigned)id % 64);
            should_warn = (sCurrentMod->behavior_lookup_warned_ids[word] & mask) == 0;
            sCurrentMod->behavior_lookup_warned_ids[word] |= mask;
        } else if (sCurrentMod) {
            should_warn = !sCurrentMod->behavior_lookup_unknown_warned;
            sCurrentMod->behavior_lookup_unknown_warned = true;
        }
        if (should_warn) {
            if (id >= 0 && (uint64_t)id < sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0]))
                mod_logf("warning", sCurrentMod ? sCurrentMod->name : NULL,
                         "get_behavior_from_id(%lld) for %s returns nil: this native behavior is absent from the engine",
                         (long long)id, sNativeBehaviors[id].name);
            else
                mod_logf("warning", sCurrentMod ? sCurrentMod->name : NULL,
                         "get_behavior_from_id(%lld) returns nil: behavior is unavailable in this engine",
                         (long long)id);
        }
        lua_pushnil(L);
        return 1;
    }
    push_behavior_pointer(L, script);
    return 1;
}

static int l_get_behavior_name_from_id(lua_State *L) {
    lua_Integer id = luaL_checkinteger(L, 1);
    ModBehavior *custom = id >= 0 && (uint64_t)id <= UINT32_MAX
        ? find_behavior((uint32_t)id) : NULL;
    if (custom) {
        if (custom->name) lua_pushstring(L, custom->name);
        else lua_pushnil(L);
        return 1;
    }
    if (id >= 0 && (uint64_t)id < sizeof(sNativeBehaviors) / sizeof(sNativeBehaviors[0])) {
        const char *name = sNativeBehaviors[id].name;
        if (name && strncmp(name, "id_", 3) == 0) name += 3;
        if (name && *name) lua_pushstring(L, name); else lua_pushnil(L);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int l_cur_obj_init_animation(lua_State *L) {
    lua_Integer animation_index = luaL_checkinteger(L, 1);
    if (!gCurrentObject)
        return luaL_error(L, "cur_obj_init_animation requires an active native object behavior callback");
    if (animation_index < 0 || animation_index > UINT8_MAX)
        return luaL_argerror(L, 1, "animation index must fit the engine's 8-bit behavior command range");
    if (!gCurrentObject->oAnimations)
        return luaL_error(L, "cur_obj_init_animation: current object has no registered animation table");
    cur_obj_init_animation((s32)animation_index);
    return 0;
}

static int l_set_object_respawn_info_bits(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_Integer bits = luaL_checkinteger(L, 2);
    if (bits < 0 || bits > UINT8_MAX)
        return luaL_argerror(L, 2, "respawn info bits must be in the range 0..255");
    if (object) set_object_respawn_info_bits(object, (u8)bits);
    return 0;
}

static int l_load_object_collision_model(lua_State *L) {
    (void)L;
    if (!gCurrentObject)
        return luaL_error(L, "load_object_collision_model requires an active native object behavior callback");
    if (!gCurrentObject->collisionData)
        return luaL_error(L, "load_object_collision_model: current object has no collision data");
    load_object_collision_model();
    return 0;
}

static int l_obj_spawn_loot_yellow_coins(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_Integer count = luaL_checkinteger(L, 2);
    float launch_speed = checked_lua_finite_float(L, 3, "yellow coin loot launch speed");
    if (count < 0 || count > 128)
        return luaL_argerror(L, 2, "coin count must be in the range 0..128");
    if (fabsf(launch_speed) > 1000.0f)
        return luaL_argerror(L, 3, "yellow coin loot launch speed must be within -1000..1000");
    if (object && count > 0) obj_spawn_loot_yellow_coins(object, (s32)count, launch_speed);
    return 0;
}

static int l_get_object_list_from_behavior(lua_State *L) {
    LuaUserdata *pointer = luaL_testudata(L, 1, "sm64.mod.Pointer");
    if (!pointer || pointer->kind != LUA_UD_POINTER ||
        pointer->field != MOD_POINTER_BEHAVIOR || !pointer->pointer)
        return luaL_argerror(L, 1, "expected a behavior pointer from get_behavior_from_id");
    const BehaviorScript *script = (const BehaviorScript *)pointer->pointer;
    ModBehavior *custom = find_behavior_by_script(script);
    if (!custom && native_behavior_id_from_script(script) >= MOD_NATIVE_BEHAVIOR_COUNT)
        return luaL_argerror(L, 1, "behavior pointer is not registered by the engine or an enabled mod");

    lua_pushinteger(L, custom ? custom->object_list : get_object_list_from_behavior(script));
    return 1;
}

static int l_obj_get_first_with_behavior_id(lua_State *L) {
    ModBehavior *custom = NULL;
    const BehaviorScript *script = behavior_from_id(L, 1, &custom);
    if (!script) return behavior_id_error(L, luaL_checkinteger(L, 1), "obj_get_first_with_behavior_id");
    int list = object_list_from_behavior_id(L, custom, script);
    struct ObjectNode *head = &gObjectLists[list];
    struct Object *object = find_object_in_list(list, head->next, custom, script);
    if (object) push_object(L, object); else lua_pushnil(L);
    return 1;
}

/* Match CoopDX's nearest-object search: inspect only the behavior's object
 * list, include every active object with the resolved behavior, and retain
 * the first object at equal distances. The native helper uses a 0x20000
 * distance ceiling; preserve that boundary here as well. */
static int l_obj_get_nearest_object_with_behavior_id(lua_State *L) {
    struct Object *origin = checked_object(L, 1);
    ModBehavior *custom = NULL;
    const BehaviorScript *script = behavior_from_id(L, 2, &custom);
    if (!script) return behavior_id_error(L, luaL_checkinteger(L, 2),
                                         "obj_get_nearest_object_with_behavior_id");

    int list = object_list_from_behavior_id(L, custom, script);
    struct ObjectNode *head = &gObjectLists[list];
    struct ObjectNode *node = head->next;
    struct Object *nearest = NULL;
    float nearest_distance = 0x20000;
    unsigned scanned = 0;
    while (node != head && node && scanned++ < OBJECT_POOL_CAPACITY) {
        struct Object *candidate = (struct Object *)node;
        ModObject *entry = custom ? find_object(candidate) : NULL;
        bool matches = custom ? (entry && entry->behavior == custom)
                              : candidate->behavior == script;
        if (matches && candidate->activeFlags != ACTIVE_FLAG_DEACTIVATED) {
            float distance = dist_between_objects(origin, candidate);
            if (distance < nearest_distance) {
                nearest = candidate;
                nearest_distance = distance;
            }
        }
        node = node->next;
    }
    if (nearest) push_object(L, nearest); else lua_pushnil(L);
    return 1;
}

static int l_obj_get_first_with_behavior_id_and_field_s32(lua_State *L) {
    (void)luaL_checkinteger(L, 1);
    lua_Integer field_index = luaL_checkinteger(L, 2);
    lua_Integer field_value = luaL_checkinteger(L, 3);
    if (field_index < 0 || field_index >= 0x50 || field_value < INT32_MIN || field_value > INT32_MAX) {
        lua_pushnil(L);
        return 1;
    }

    ModBehavior *custom = NULL;
    const BehaviorScript *script = behavior_from_id(L, 1, &custom);
    if (!script) { lua_pushnil(L); return 1; }
    int list = object_list_from_behavior_id(L, custom, script);
    struct ObjectNode *head = &gObjectLists[list];
    struct ObjectNode *node = head->next;
    unsigned scanned = 0;
    while (node != head && node && scanned++ < OBJECT_POOL_CAPACITY) {
        struct Object *object = (struct Object *)node;
        ModObject *entry = custom ? find_object(object) : NULL;
        bool matches_behavior = custom ? (entry && entry->behavior == custom)
                                       : object->behavior == script;
        if (matches_behavior && object->activeFlags != ACTIVE_FLAG_DEACTIVATED &&
            object->rawData.asS32[field_index] == (s32)field_value) {
            push_object(L, object);
            return 1;
        }
        node = node->next;
    }
    lua_pushnil(L);
    return 1;
}

/* CoopDX's object field APIs address the 0x50-entry rawData union, not byte
 * offsets into struct Object. Keep the bounds tied to our actual layout. */
static size_t object_field_count(void) {
    struct Object *object = NULL;
    return sizeof(object->rawData.asU32) / sizeof(object->rawData.asU32[0]);
}

static int l_obj_get_field_u32(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_Integer field_index = luaL_checkinteger(L, 2);
    if (!object || field_index < 0 || (uint64_t)field_index >= object_field_count()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, (lua_Integer)object->rawData.asU32[field_index]);
    return 1;
}

static int l_obj_set_field_u32(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_Integer field_index = luaL_checkinteger(L, 2);
    lua_Integer value = luaL_checkinteger(L, 3);
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return luaL_argerror(L, 3, "u32 object field value must be between 0 and 4294967295");
    if (object && field_index >= 0 && (uint64_t)field_index < object_field_count())
        object->rawData.asU32[field_index] = (u32)value;
    return 0;
}

static int l_obj_get_next_with_same_behavior_id(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    ModObject *entry = find_object(object);
    ModBehavior *custom = entry ? entry->behavior : NULL;
    const BehaviorScript *script = custom ? (const BehaviorScript *)custom->script : object->behavior;
    if (!script) { lua_pushnil(L); return 1; }
    int list = object_list_from_behavior_id(L, custom, script);
    struct Object *next = find_object_in_list(list, object->header.next, custom, script);
    if (next) push_object(L, next); else lua_pushnil(L);
    return 1;
}

static int l_obj_get_first(lua_State *L) {
    lua_Integer list_value = luaL_checkinteger(L, 1);
    if (!gObjectLists || list_value < 0 || list_value >= NUM_OBJ_LISTS) {
        lua_pushnil(L);
        return 1;
    }
    int list = (int)list_value;
    struct Object *object = find_object_in_list(list, gObjectLists[list].next, NULL, NULL);
    if (object) push_object(L, object); else lua_pushnil(L);
    return 1;
}

static int l_obj_get_next(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    int list = -1;
    if (gObjectLists) {
        for (int i = 0; i < NUM_OBJ_LISTS; ++i) {
            struct ObjectNode *head = &gObjectLists[i];
            struct ObjectNode *node = head->next;
            unsigned scanned = 0;
            while (node != head && node && scanned++ < OBJECT_POOL_CAPACITY) {
                if ((struct Object *)node == object) { list = i; break; }
                node = node->next;
            }
            if (list >= 0) break;
        }
    }
    if (list < 0) { lua_pushnil(L); return 1; }
    struct ObjectNode *head = &gObjectLists[list];
    struct ObjectNode *node = object->header.next;
    unsigned scanned = 0;
    struct Object *next = NULL;
    while (node != head && node && scanned++ < OBJECT_POOL_CAPACITY) {
        struct Object *candidate = (struct Object *)node;
        if (candidate->activeFlags != ACTIVE_FLAG_DEACTIVATED) { next = candidate; break; }
        node = node->next;
    }
    if (next) push_object(L, next); else lua_pushnil(L);
    return 1;
}

static int l_obj_mark_for_deletion(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    obj_mark_for_deletion(object);
    return 0;
}

static int l_obj_has_behavior_id(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    ModBehavior *custom = NULL;
    const BehaviorScript *script = behavior_from_id(L, 2, &custom);
    (void)custom;
    /* CoopDX returns 0 for null objects and for IDs with no behavior. */
    lua_pushinteger(L, object && script && object->behavior == script ? 1 : 0);
    return 1;
}

static int l_obj_is_grabbable(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    bool grabbable = object && (object->oInteractType & INTERACT_GRABBABLE) != 0 &&
                     (object->oInteractionSubtype & INT_SUBTYPE_NOT_GRABBABLE) == 0;
    lua_pushboolean(L, grabbable);
    return 1;
}

static int l_obj_angle_to_object(lua_State *L) {
    struct Object *from = checked_object(L, 1);
    struct Object *to = checked_object(L, 2);
    lua_pushinteger(L, obj_angle_to_object(from, to));
    return 1;
}

static int l_obj_is_mushroom_1up(lua_State *L) {
    static const lua_Integer one_up_behavior_ids[] = { 0, 1, 2, 3, 4, 209, 210, 211, 212, 213 };
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    bool one_up = false;
    if (object && !(object->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE)) {
        for (size_t i = 0; i < sizeof(one_up_behavior_ids) / sizeof(one_up_behavior_ids[0]); ++i) {
            const BehaviorScript *script = native_behavior_from_id(one_up_behavior_ids[i]);
            if (script && object_behavior_script(object) == script) {
                one_up = true;
                break;
            }
        }
    }
    lua_pushboolean(L, one_up);
    return 1;
}

static int l_obj_check_hitbox_overlap(lua_State *L) {
    struct Object *first = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    struct Object *second = lua_isnoneornil(L, 2) ? NULL : checked_object(L, 2);
    bool overlaps = false;
    if (first && second) {
        float first_height = first->hitboxHeight > first->hurtboxHeight
            ? first->hitboxHeight : first->hurtboxHeight;
        float first_radius = first->hitboxRadius > first->hurtboxRadius
            ? first->hitboxRadius : first->hurtboxRadius;
        float second_height = second->hitboxHeight > second->hurtboxHeight
            ? second->hitboxHeight : second->hurtboxHeight;
        float second_radius = second->hitboxRadius > second->hurtboxRadius
            ? second->hitboxRadius : second->hurtboxRadius;
        float combined_radius = first_radius + second_radius;
        float dx = first->oPosX - second->oPosX;
        float dz = first->oPosZ - second->oPosZ;
        if (dx * dx + dz * dz <= combined_radius * combined_radius) {
            float first_bottom = first->oPosY - first->hitboxDownOffset;
            float first_top = first_bottom + first_height;
            float second_bottom = second->oPosY - second->hitboxDownOffset;
            float second_top = second_bottom + second_height;
            float combined_height = first_height + second_height;
            overlaps = (second_top - first_bottom) <= combined_height &&
                       (first_top - second_bottom) <= combined_height;
        }
    }
    lua_pushboolean(L, overlaps);
    return 1;
}

static int l_obj_is_valid_for_interaction(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    bool valid = object && object->activeFlags != ACTIVE_FLAG_DEACTIVATED &&
                 object->oIntangibleTimer == 0 &&
                 (object->oInteractStatus & INT_STATUS_INTERACTED) == 0;
    lua_pushboolean(L, valid);
    return 1;
}

static int l_obj_is_attackable(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    bool attackable = object && (object->oInteractType &
        (INTERACT_KOOPA | INTERACT_BOUNCE_TOP | INTERACT_BOUNCE_TOP2 | INTERACT_HIT_FROM_BELOW)) != 0;
    lua_pushboolean(L, attackable);
    return 1;
}

static int l_obj_is_bully(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_pushboolean(L, object && (object->oInteractType & INTERACT_BULLY) != 0);
    return 1;
}

static int l_obj_is_coin(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_pushboolean(L, object && (object->oInteractType & INTERACT_COIN) != 0);
    return 1;
}

static int l_obj_is_exclamation_box(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    const BehaviorScript *exclamation_box = native_behavior_from_id(159);
    lua_pushboolean(L, object && exclamation_box &&
                    object_behavior_script(object) == exclamation_box && object->oAction == 2);
    return 1;
}

static int l_obj_is_secret(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    const BehaviorScript *hidden_star_trigger = native_behavior_from_id(220);
    lua_pushboolean(L, object && hidden_star_trigger &&
                    object_behavior_script(object) == hidden_star_trigger);
    return 1;
}

static int l_obj_set_model_extended(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_Integer model_id = luaL_checkinteger(L, 2);
    if (!object) return 0; /* Matches CoopDX's null-object no-op. */
    int native_model = native_model_from_id(L, model_id, "obj_set_model_extended");
    object->header.gfx.sharedChild = native_model == MODEL_NONE
        ? NULL : gLoadedGraphNodes[native_model];
    mod_runtime_object_set_model(object, native_model, (int)model_id);
    return 0;
}

#ifdef MOD_RUNTIME_HOST_TEST
bool mod_runtime_host_set_object_model(struct Object *object, int extended_id,
                                       char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (!sLua || !object || extended_id < 0) return false;
    lua_settop(sLua, 0);
    push_object(sLua, object);
    lua_pushinteger(sLua, extended_id);
    lua_pushcfunction(sLua, l_obj_set_model_extended);
    lua_insert(sLua, 1);
    if (lua_pcall(sLua, 2, 0, 0) != LUA_OK) {
        if (error && error_size)
            snprintf(error, error_size, "%s", lua_tostring(sLua, -1));
        lua_settop(sLua, 0);
        return false;
    }
    lua_settop(sLua, 0);
    return true;
}

bool mod_runtime_host_set_animation(struct Object *object, const char *name) {
    if (!sLua || !object || !name || !name[0]) return false;
    int base = lua_gettop(sLua);
    lua_getglobal(sLua, "smlua_anim_util_set_animation");
    if (!lua_isfunction(sLua, -1)) {
        lua_settop(sLua, base);
        return false;
    }
    push_object(sLua, object);
    lua_pushstring(sLua, name);
    int status = lua_pcall(sLua, 2, 0, 0);
    lua_settop(sLua, base);
    if (status != LUA_OK) return false;
    ModAnimation *animation = find_custom_animation(name);
    return animation && object->header.gfx.unk38.curAnim == animation->animation;
}

bool mod_runtime_host_get_lua_integer(const char *name, int64_t *value) {
    if (!sLua || !name || !name[0] || !value) return false;
    int base = lua_gettop(sLua);
    /* Script globals live in a per-mod environment, not Lua's base global
     * table. Read the first loaded environment directly so absent names remain
     * a non-throwing probe even with the sandbox's strict __index handler. */
    Mod *mod = sMods;
    while (mod && (mod->env_ref == LUA_NOREF || !mod->enabled)) mod = mod->next;
    if (!mod) { lua_settop(sLua, base); return false; }
    lua_rawgeti(sLua, LUA_REGISTRYINDEX, mod->env_ref);
    lua_pushstring(sLua, name);
    lua_rawget(sLua, -2);
    lua_remove(sLua, -2);
    int is_integer = 0;
    lua_Integer result = lua_tointegerx(sLua, -1, &is_integer);
    lua_settop(sLua, base);
    if (!is_integer) return false;
    *value = (int64_t)result;
    return true;
}

#endif

static int l_obj_has_model_extended(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    lua_Integer model_id = luaL_checkinteger(L, 2);
    if (!object) {
        lua_pushboolean(L, false);
        return 1;
    }

    for (DynosModel *dynamic = sDynosModels; dynamic; dynamic = dynamic->next_global) {
        if ((uint64_t)model_id == dynamic->extended_id) {
            lua_pushboolean(L, object->header.gfx.sharedChild == dynamic->graph);
            return 1;
        }
    }

    const size_t model_count = sizeof(sNativeModels) / sizeof(sNativeModels[0]);
    if (model_id < 0 || (uint64_t)model_id >= model_count ||
        sNativeModels[model_id].id != model_id || sNativeModels[model_id].native_model < 0) {
        if (sCurrentMod && !sCurrentMod->extended_model_warned) {
            sCurrentMod->extended_model_warned = true;
            if (model_id >= 0 && (uint64_t)model_id < model_count && sNativeModels[model_id].native_model < 0)
                mod_logf("warn", sCurrentMod->name,
                         "obj_has_model_extended('%s') cannot inspect this DynOS-only model until its asset is decoded and registered; returning false",
                         sNativeModels[model_id].name);
            else
                mod_logf("warn", sCurrentMod->name,
                         "obj_has_model_extended(%lld) refers to an unknown or inactive extended model; returning false",
                         (long long)model_id);
        }
        lua_pushboolean(L, false);
        return 1;
    }

    int native_model = sNativeModels[model_id].native_model;
    bool matches = native_model == MODEL_NONE
        ? object->header.gfx.sharedChild == NULL
        : gLoadedGraphNodes && native_model < 0x100 && gLoadedGraphNodes[native_model] &&
          object->header.gfx.sharedChild == gLoadedGraphNodes[native_model];
    lua_pushboolean(L, matches);
    return 1;
}

static int l_obj_scale_random(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    float range_length = checked_lua_finite_float(L, 2, "obj_scale_random rangeLength");
    float minimum_scale = checked_lua_finite_float(L, 3, "obj_scale_random minScale");
    obj_scale_random(object, range_length, minimum_scale);
    return 0;
}

static int l_obj_scale_xyz(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    float x_scale = checked_lua_finite_float(L, 2, "obj_scale_xyz xScale");
    float y_scale = checked_lua_finite_float(L, 3, "obj_scale_xyz yScale");
    float z_scale = checked_lua_finite_float(L, 4, "obj_scale_xyz zScale");
    obj_scale_xyz(object, x_scale, y_scale, z_scale);
    return 0;
}

static int l_obj_scale(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    float scale = checked_lua_finite_float(L, 2, "obj_scale scale");
    obj_scale(object, scale);
    return 0;
}

static int l_obj_set_pos(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    lua_Integer coordinates[3];
    const char *names[3] = { "obj_set_pos x", "obj_set_pos y", "obj_set_pos z" };
    for (int i = 0; i < 3; ++i) {
        coordinates[i] = luaL_checkinteger(L, i + 2);
        if (coordinates[i] < INT16_MIN || coordinates[i] > INT16_MAX)
            return luaL_argerror(L, i + 2, names[i]);
    }
    obj_set_pos(object, (s16)coordinates[0], (s16)coordinates[1], (s16)coordinates[2]);
    return 0;
}

static int l_obj_set_angle(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    lua_Integer angles[3];
    const char *names[3] = { "obj_set_angle pitch", "obj_set_angle yaw", "obj_set_angle roll" };
    for (int i = 0; i < 3; ++i) {
        angles[i] = luaL_checkinteger(L, i + 2);
        if (angles[i] < INT16_MIN || angles[i] > INT16_MAX)
            return luaL_argerror(L, i + 2, names[i]);
    }
    obj_set_angle(object, (s16)angles[0], (s16)angles[1], (s16)angles[2]);
    return 0;
}

static int l_obj_set_face_angle(lua_State *L) {
    struct Object *object = checked_object(L, 1);
    lua_Integer angles[3];
    const char *names[3] = { "obj_set_face_angle pitch", "obj_set_face_angle yaw", "obj_set_face_angle roll" };
    for (int i = 0; i < 3; ++i) {
        angles[i] = luaL_checkinteger(L, i + 2);
        if (angles[i] < INT16_MIN || angles[i] > INT16_MAX)
            return luaL_argerror(L, i + 2, names[i]);
    }
    object->oFaceAnglePitch = (s16)angles[0];
    object->oFaceAngleYaw = (s16)angles[1];
    object->oFaceAngleRoll = (s16)angles[2];
    return 0;
}

static int l_obj_set_vel(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    float velocity[3];
    const char *names[3] = { "obj_set_vel x velocity", "obj_set_vel y velocity", "obj_set_vel z velocity" };
    for (int i = 0; i < 3; ++i)
        velocity[i] = checked_lua_finite_float(L, i + 2, names[i]);
    if (object) {
        object->oVelX = velocity[0];
        object->oVelY = velocity[1];
        object->oVelZ = velocity[2];
    }
    return 0;
}

static int l_obj_move_xyz(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    float offset[3];
    const char *names[3] = { "obj_move_xyz x offset", "obj_move_xyz y offset", "obj_move_xyz z offset" };
    for (int i = 0; i < 3; ++i)
        offset[i] = checked_lua_finite_float(L, i + 2, names[i]);
    if (object) {
        object->oPosX += offset[0];
        object->oPosY += offset[1];
        object->oPosZ += offset[2];
    }
    return 0;
}

static int l_find_ceil_height(lua_State *L) {
    float x = checked_lua_finite_float(L, 1, "find_ceil_height x");
    float y = checked_lua_finite_float(L, 2, "find_ceil_height y");
    float z = checked_lua_finite_float(L, 3, "find_ceil_height z");
    struct Surface *ceiling = NULL;
    lua_pushnumber(L, find_ceil(x, y, z, &ceiling));
    return 1;
}

static int l_find_floor_height(lua_State *L) {
    float x = checked_lua_finite_float(L, 1, "find_floor_height x");
    float y = checked_lua_finite_float(L, 2, "find_floor_height y");
    float z = checked_lua_finite_float(L, 3, "find_floor_height z");
    struct Surface *floor = NULL;
    lua_pushnumber(L, find_floor(x, y, z, &floor));
    return 1;
}

static int l_find_water_level(lua_State *L) {
    float x = checked_lua_finite_float(L, 1, "water query x");
    float z = checked_lua_finite_float(L, 2, "water query z");
    lua_pushnumber(L, find_water_level(x, z));
    return 1;
}

static ModObject *track_object(struct Object *object, ModBehavior *behavior, Mod *owner) {
    ModObject *record = calloc(1, sizeof(*record));
    if (!record) return NULL;
    record->object = object; record->owner = owner; record->behavior = behavior;
    record->enabled = true; record->next = sObjects; sObjects = record;
    return record;
}

static bool mod_object_capacity_available(const Mod *owner) {
    unsigned total = 0, owned = 0;
    for (const ModObject *entry = sObjects; entry; entry = entry->next) {
        ++total;
        if (entry->owner == owner) ++owned;
    }
    return owner && total < MOD_MAX_TRACKED_OBJECTS && owned < MOD_MAX_OWNED_OBJECTS;
}

static struct Object *lua_spawn(lua_State *L, struct Object *parent, int model, uint32_t behavior_id, bool absolute,
                               float x, float y, float z, int pitch, int yaw, int roll) {
    if (model < 0 || model >= 0x100) {
        luaL_error(sLua, "model id %d is outside the native engine model table; DynOS model ids are unsupported", model);
        return NULL;
    }
    ModBehavior *behavior = find_behavior(behavior_id);
    if (!behavior) return NULL;
    const BehaviorScript *script = (const BehaviorScript *)behavior->script;
    struct Object *object = absolute
        ? spawn_object_abs_with_rot(parent, 0, (s16)model, script, (s16)x, (s16)y, (s16)z,
                                    (s16)pitch, (s16)yaw, (s16)roll)
        : spawn_object(parent, model, script);
    if (object && !track_object(object, behavior, behavior->owner)) {
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

/* CoopDX's local-only object API: model arguments are ModelExtendedId values,
 * not this engine's MODEL_* values. Dynamic IDs need DynOS registration and
 * are rejected by native_model_from_id until that subsystem exists. */
static int l_spawn_non_sync_object(lua_State *L) {
    if (!sCurrentMod) return luaL_error(L, "spawn_non_sync_object must be called while a mod is executing");
    if (!sCurrentMod->loaded) {
        mod_log("warning", sCurrentMod->name,
                "spawn_non_sync_object is unavailable while mod startup scripts are loading");
        lua_pushnil(L);
        return 1;
    }

    lua_Integer behavior_id = luaL_checkinteger(L, 1);
    lua_Integer model_id = luaL_checkinteger(L, 2);
    float x = checked_lua_finite_float(L, 3, "spawn x");
    float y = checked_lua_finite_float(L, 4, "spawn y");
    float z = checked_lua_finite_float(L, 5, "spawn z");
    if (!lua_isnoneornil(L, 6)) luaL_checktype(L, 6, LUA_TFUNCTION);

    ModBehavior *custom_behavior = NULL;
    const BehaviorScript *script = behavior_from_id(L, 1, &custom_behavior);
    if (!script) return behavior_id_error(L, behavior_id, "spawn_non_sync_object");
    int model = native_model_from_id(L, model_id, "spawn_non_sync_object");
    struct Object *parent = gMarioState ? gMarioState->marioObj : NULL;
    if (!parent) return luaL_error(L, "spawn_non_sync_object: Mario's parent object is unavailable");
    if (!mod_object_capacity_available(sCurrentMod))
        return luaL_error(L, "spawn_non_sync_object: mod-owned object limit reached (%d per mod, %d total)",
                          (int)MOD_MAX_OWNED_OBJECTS, (int)MOD_MAX_TRACKED_OBJECTS);

    Mod *owner = sCurrentMod;
    const char *file = sCurrentFile ? sCurrentFile : "main.lua";
    int setup_ref = LUA_NOREF;
    if (lua_isfunction(L, 6)) {
        lua_pushvalue(L, 6);
        setup_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    struct Object *object = spawn_object(parent, model, script);
    if (!object) {
        if (setup_ref != LUA_NOREF) unref_if_valid(setup_ref);
        lua_pushnil(L);
        return 1;
    }

    /* CoopDX creates these as local-only actors with self-parenting and their
     * requested position copied to both current and home coordinates. */
    object->parentObj = object;
    object->oPosX = object->oHomeX = x;
    object->oPosY = object->oHomeY = y;
    object->oPosZ = object->oHomeZ = z;
    if (!track_object(object, custom_behavior, owner)) {
        obj_mark_for_deletion(object);
        if (setup_ref != LUA_NOREF) unref_if_valid(setup_ref);
        return luaL_error(L, "spawn_non_sync_object: out of memory tracking mod-owned object lifecycle");
    }

    if (setup_ref != LUA_NOREF) {
        push_object(L, object);
        bool setup_ok = call_ref(owner, file, setup_ref, "spawn_non_sync_object setup", 1, 0);
        unref_if_valid(setup_ref);
        if (!setup_ok) {
            obj_mark_for_deletion(object);
            mod_runtime_object_unload(object);
            lua_pushnil(L);
            return 1;
        }
    }

    push_object(L, object);
    return 1;
}

static int l_spawn_sync_object_unsupported(lua_State *L) {
    return luaL_error(L, "spawn_sync_object requires CoopDX multiplayer object synchronization, which this single-player engine does not implement");
}

static int l_djui_hud_set_resolution(lua_State *L) {
    int resolution = (int)luaL_checkinteger(L, 1);
    if (!mod_hud_set_resolution(resolution))
        return luaL_error(L, "djui_hud_set_resolution: expected RESOLUTION_DJUI (0) or RESOLUTION_N64 (1)");
    return 0;
}

static double mod_runtime_monotonic_seconds(void) {
    struct timespec now;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
#endif
    return 0.0;
}

static void remove_mod_popups(Mod *owner) {
    for (size_t i = 0; i < MOD_MAX_POPUPS; ++i) {
        if (sPopups[i].owner == owner) memset(&sPopups[i], 0, sizeof(sPopups[i]));
    }
}

static void queue_mod_popup(Mod *owner, const char *message, size_t length, int lines, bool play_notification) {
    double now = mod_runtime_monotonic_seconds();
    ModPopup *slot = NULL;
    for (size_t i = 0; i < MOD_MAX_POPUPS; ++i) {
        if (sPopups[i].active && now - sPopups[i].created_at >= 6.0)
            memset(&sPopups[i], 0, sizeof(sPopups[i]));
        if (!slot && !sPopups[i].active) slot = &sPopups[i];
    }
    if (!slot) {
        slot = &sPopups[0];
        for (size_t i = 1; i < MOD_MAX_POPUPS; ++i)
            if (sPopups[i].created_at < slot->created_at) slot = &sPopups[i];
    }
    slot->owner = owner;
    memcpy(slot->text, message, length);
    slot->text[length] = '\0';
    slot->lines = lines;
    slot->created_at = now;
    slot->active = true;

    if (!owner->popup_markup_warned && strstr(message, "\\#")) {
        owner->popup_markup_warned = true;
        mod_log("warning", owner->name,
                "native HUD text does not interpret CoopDX rich-color markup");
    }
    if (play_notification) play_sound(SOUND_MENU_PINCH_MARIO_FACE, gGlobalSoundSource);
}

static int l_djui_popup_create(lua_State *L) {
    if (!sCurrentMod) return luaL_error(L, "djui_popup_create must be called from a loaded mod callback");
    size_t length = 0;
    const char *message = luaL_checklstring(L, 1, &length);
    lua_Integer lines = luaL_checkinteger(L, 2);
    if (length >= MOD_MAX_POPUP_TEXT)
        return luaL_argerror(L, 1, "popup text must be shorter than 256 bytes");
    if (lines < 1 || lines > 8)
        return luaL_argerror(L, 2, "popup lines must be between 1 and 8 for this bounded HUD adapter");

    queue_mod_popup(sCurrentMod, message, length, (int)lines, true);
    return 0;
}

static int l_djui_chat_message_create(lua_State *L) {
    if (!sCurrentMod) return luaL_error(L, "djui_chat_message_create must be called from a loaded mod callback");
    size_t length = 0;
    const char *message = luaL_checklstring(L, 1, &length);
    if (length >= MOD_MAX_POPUP_TEXT)
        return luaL_argerror(L, 1, "chat message must be shorter than 256 bytes in this bounded HUD adapter");
    int lines = 1;
    for (size_t i = 0; i < length; ++i) if (message[i] == '\n') ++lines;
    if (lines > 8)
        return luaL_argerror(L, 1, "chat message exceeds the 8-line native HUD limit");
    queue_mod_popup(sCurrentMod, message, length, lines, false);
    if (!sCurrentMod->chat_popup_warned) {
        sCurrentMod->chat_popup_warned = true;
        mod_log("warning", sCurrentMod->name,
                "DJUI chat history is unavailable; djui_chat_message_create is displayed as a temporary native HUD popup");
    }
    return 0;
}

static void render_mod_popups(void) {
    double now = mod_runtime_monotonic_seconds();
    unsigned visible = 0;
    for (size_t i = 0; i < MOD_MAX_POPUPS; ++i) {
        ModPopup *popup = &sPopups[i];
        if (!popup->active) continue;
        if (!popup->owner || !popup->owner->enabled || now - popup->created_at >= 6.0) {
            memset(popup, 0, sizeof(*popup));
            continue;
        }

        /* CoopDX uses a 400px-wide, top-right popup with 32px per requested
         * line and a 6-second lifetime. Keep the same placement/expiry while
         * rendering via the existing native HUD queue and Metal display list. */
        mod_hud_set_resolution(0);
        mod_hud_set_font(0);
        const float width = 400.0f;
        const float height = (float)popup->lines * 32.0f + 32.0f;
        const float x = (float)mod_hud_get_screen_width() - width - 16.0f;
        const float y = 16.0f + (float)visible * (height + 8.0f);
        double remaining = 6.0 - (now - popup->created_at);
        float fade = remaining < 1.0 ? (float)remaining : 1.0f;
        if (fade < 0.0f) fade = 0.0f;

        mod_hud_set_color(0, 0, 0, (u8)(220.0f * fade));
        bool rect_ok = mod_hud_render_rect(x, y, width, height);
        mod_hud_set_color(220, 220, 220, (u8)(255.0f * fade));
        bool text_ok = mod_hud_print_text(popup->text, x + 16.0f, y + 16.0f, 1.0f, 1.0f);
        mod_hud_reset_color();
        if ((!rect_ok || !text_ok) && !popup->render_warning_reported) {
            mod_log("warning", popup->owner->name,
                    "djui_popup_create: native HUD command budget or font adapter rejected popup rendering");
            popup->render_warning_reported = true;
        }
        ++visible;
    }
}

static int l_djui_hud_set_font(lua_State *L) {
    int font = (int)luaL_checkinteger(L, 1);
    if (!mod_hud_set_font(font))
        return luaL_error(L, "djui_hud_set_font: unsupported font ID %d (expected CoopDX FONT_LEGACY through FONT_SPECIAL)", font);
    if (font != 0 && sCurrentMod && !sCurrentMod->hud_font_fallback_warned) {
        sCurrentMod->hud_font_fallback_warned = true;
        mod_logf("warning", sCurrentMod->name,
                 "DJUI font %d is rendered with the native SM64 IA font; font artwork and metrics differ from CoopDX",
                 font);
    }
    return 0;
}

static int l_djui_hud_get_screen_width(lua_State *L) {
    lua_pushinteger(L, mod_hud_get_screen_width());
    return 1;
}

static int l_djui_hud_get_screen_height(lua_State *L) {
    lua_pushinteger(L, mod_hud_get_screen_height());
    return 1;
}

static int l_djui_hud_set_color(lua_State *L) {
    u8 color[4];
    for (int i = 0; i < 4; ++i) {
        /* CoopDX's LuaJIT converter accepts numeric values and converts them
         * to lua_Integer before the native u8 cast. Lua 5.3's
         * luaL_checkinteger rejects fractional numbers, which OMM's HUD uses
         * for faded alpha/color values. Keep the native integer conversion
         * semantics while validating the range before narrowing. */
        if (!lua_isnumber(L, i + 1))
            return luaL_argerror(L, i + 1, "RGBA component must be a number");
        lua_Number number = lua_tonumber(L, i + 1);
        if (!isfinite((double)number) || number < 0.0 || number > 255.0)
            return luaL_error(L, "djui_hud_set_color: RGBA components must be in the range 0..255");
        lua_Integer component = (lua_Integer)number;
        color[i] = (u8)component;
    }
    mod_hud_set_color(color[0], color[1], color[2], color[3]);
    return 0;
}

static int l_djui_hud_set_rotation(lua_State *L) {
    lua_Integer rotation = luaL_checkinteger(L, 1);
    float pivot_x = checked_lua_finite_float(L, 2, "HUD rotation pivot X");
    float pivot_y = checked_lua_finite_float(L, 3, "HUD rotation pivot Y");
    if (fabsf(pivot_x) > 64.0f || fabsf(pivot_y) > 64.0f)
        return luaL_error(L, "djui_hud_set_rotation: pivot coordinates must be within -64..64");
    /* CoopDX's binding accepts an s16 angle. Keep its low 16 bits so both
     * signed angles and frame-based values that wrap through 0xFFFF work. */
    mod_hud_set_rotation((u16)(u64)rotation, pivot_x, pivot_y);
    return 0;
}

static int l_djui_hud_reset_color(lua_State *L) {
    (void)L;
    mod_hud_reset_color();
    return 0;
}

static int l_djui_hud_measure_text(lua_State *L) {
    size_t length = 0;
    const char *text = luaL_checklstring(L, 1, &length);
    if (length > 255) return luaL_error(L, "djui_hud_measure_text: text exceeds the 255-byte HUD limit");
    float width = 0.0f, height = 0.0f;
    if (!mod_hud_measure_text(text, &width, &height))
        return luaL_error(L, "djui_hud_measure_text: text could not be measured by the native font adapter");
    lua_pushnumber(L, width);
    lua_pushnumber(L, height);
    return 2;
}

static int l_djui_hud_render_rect(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float width = (float)luaL_checknumber(L, 3);
    float height = (float)luaL_checknumber(L, 4);
    if (!mod_hud_render_rect(x, y, width, height))
        return luaL_error(L, "djui_hud_render_rect: invalid rectangle or per-frame HUD command limit reached (256)");
    return 0;
}

static int l_djui_hud_print_text(lua_State *L) {
    size_t length = 0;
    const char *text = luaL_checklstring(L, 1, &length);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float scale_x = lua_isnoneornil(L, 4) ? 1.0f : (float)luaL_checknumber(L, 4);
    float scale_y = lua_isnoneornil(L, 5) ? scale_x : (float)luaL_checknumber(L, 5);
    if (length > 255) return luaL_error(L, "djui_hud_print_text: text exceeds the 255-byte HUD limit");
    if (!mod_hud_print_text(text, x, y, scale_x, scale_y))
        return luaL_error(L, "djui_hud_print_text: invalid font/scale or per-frame HUD command limit reached (256)");
    return 0;
}

/* CoopDX scripts use angle helpers in the engine's 16-bit binary-angle
 * domain. Delegate to SM64's own lookup-table implementation so Lua mods
 * observe the same rounding and quadrant behavior as native gameplay. */
static int l_atan2s(lua_State *L) {
    float y = checked_lua_finite_float(L, 1, "atan2s y");
    float x = checked_lua_finite_float(L, 2, "atan2s x");
    lua_pushinteger(L, atan2s(y, x));
    return 1;
}

static s16 checked_wrapped_s16(lua_State *L, int index) {
    lua_Integer value = luaL_checkinteger(L, index);
    uint16_t low = (uint16_t)(uint64_t)value;
    int32_t signed_value = low <= INT16_MAX ? (int32_t)low : (int32_t)low - 0x10000;
    return (s16)signed_value;
}

static int l_sins(lua_State *L) {
    lua_pushnumber(L, sins(checked_wrapped_s16(L, 1)));
    return 1;
}

static int l_coss(lua_State *L) {
    lua_pushnumber(L, coss(checked_wrapped_s16(L, 1)));
    return 1;
}

static int l_approach_s32(lua_State *L) {
    lua_Integer current = luaL_checkinteger(L, 1);
    lua_Integer target = luaL_checkinteger(L, 2);
    lua_Integer inc = luaL_checkinteger(L, 3);
    lua_Integer dec = luaL_checkinteger(L, 4);
    if (current < INT32_MIN || current > INT32_MAX || target < INT32_MIN || target > INT32_MAX ||
        inc < 0 || inc > INT32_MAX || dec < 0 || dec > INT32_MAX)
        return luaL_error(L, "approach_s32 expects signed 32-bit values and nonnegative increments");

    /* The native routine has an intentional C overflow edge at INT32 limits.
     * Preserve its normal step/clip behavior while taking the clip result
     * directly when a requested step would reach or pass the target. */
    int64_t distance = current < target ? target - current : current - target;
    int64_t step = current < target ? inc : dec;
    if (step >= distance) lua_pushinteger(L, target);
    else lua_pushinteger(L, approach_s32((s32)current, (s32)target, (s32)inc, (s32)dec));
    return 1;
}

static void check_vec3_argument(lua_State *L, int index, float out[3], const char *api) {
    LuaUserdata *ud = luaL_testudata(L, index, "sm64.mod.Vec3f");
    if (ud) {
        if (ud->kind != LUA_UD_VEC_F && ud->kind != LUA_UD_VEC_F_SNAPSHOT)
            luaL_argerror(L, index, "expected a Vec3f");
        float *values = ud->kind == LUA_UD_VEC_F_SNAPSHOT ? ud->vector_snapshot : vec_f_pointer(ud);
        if (ud->kind == LUA_UD_VEC_F && !values &&
            (ud->field == VEC_OBJECT_CAMERA_TO_OBJECT || ud->field == VEC_OBJECT_SHADOW_POSITION))
            luaL_argerror(L, index, "stale object vector");
        for (int axis = 0; axis < 3; ++axis) out[axis] = values ? values[axis] : 0.0f;
    } else if ((ud = luaL_testudata(L, index, "sm64.mod.Vec3s")) != NULL) {
        if (ud->kind != LUA_UD_VEC_S && ud->kind != LUA_UD_VEC_S_SNAPSHOT)
            luaL_argerror(L, index, "expected a Vec3s");
        int16_t *values = ud->kind == LUA_UD_VEC_S_SNAPSHOT ? ud->short_vector_snapshot : vec_s_pointer(ud);
        if (!values) luaL_argerror(L, index, "stale object vector");
        for (int axis = 0; axis < 3; ++axis) out[axis] = (float)values[axis];
    } else if (lua_istable(L, index)) {
        static const char *const axes[] = { "x", "y", "z" };
        for (int axis = 0; axis < 3; ++axis) {
            lua_getfield(L, index, axes[axis]);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_rawgeti(L, index, axis + 1);
            }
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                luaL_error(L, "%s: argument %d must contain numeric x/y/z or [1..3] coordinates",
                           api, index);
            }
            out[axis] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
            if (!isfinite(out[axis]))
                luaL_error(L, "%s: argument %d coordinates must be finite", api, index);
        }
    } else {
        luaL_argerror(L, index, "expected a Vec3f, Vec3s, or x/y/z vector table");
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!isfinite(out[axis]))
            luaL_error(L, "%s: argument %d coordinates must be finite", api, index);
    }
}

static void write_vec3_table(lua_State *L, int index, const float values[3]) {
    static const char *const axes[] = { "x", "y", "z" };
    index = lua_absindex(L, index);
    lua_getfield(L, index, "x");
    bool named = !lua_isnil(L, -1);
    lua_pop(L, 1);
    for (int axis = 0; axis < 3; ++axis) {
        lua_pushnumber(L, values[axis]);
        if (named) lua_setfield(L, index, axes[axis]);
        else lua_rawseti(L, index, axis + 1);
    }
}

static float *check_mutable_vec3_userdata(lua_State *L, int index, const char *api) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.Vec3f");
    if (ud->kind != LUA_UD_VEC_F)
        luaL_error(L, "%s expects a mutable Vec3f or vector table", api);
    float *values = vec_f_pointer(ud);
    if (!values && (ud->field == VEC_OBJECT_CAMERA_TO_OBJECT ||
                    ud->field == VEC_OBJECT_SHADOW_POSITION))
        luaL_error(L, "%s received a stale object vector", api);
    return values;
}

static int l_vec3f_set(lua_State *L) {
    float values[3] = {
        checked_lua_finite_float(L, 2, "vec3f_set x"),
        checked_lua_finite_float(L, 3, "vec3f_set y"),
        checked_lua_finite_float(L, 4, "vec3f_set z"),
    };
    if (lua_istable(L, 1)) {
        write_vec3_table(L, 1, values);
    } else {
        float *destination = check_mutable_vec3_userdata(L, 1, "vec3f_set");
        vec3f_set(destination, values[0], values[1], values[2]);
    }
    return 0;
}

static int l_vec3f_copy(lua_State *L) {
    float values[3];
    check_vec3_argument(L, 2, values, "vec3f_copy");
    if (lua_istable(L, 1)) {
        write_vec3_table(L, 1, values);
    } else {
        float *destination = check_mutable_vec3_userdata(L, 1, "vec3f_copy");
        vec3f_copy(destination, values);
    }
    return 0;
}

static int l_vec3f_normalize(lua_State *L) {
    if (lua_istable(L, 1)) {
        float values[3];
        check_vec3_argument(L, 1, values, "vec3f_normalize");
        vec3f_normalize(values);
        write_vec3_table(L, 1, values);
    } else {
        float *values = check_mutable_vec3_userdata(L, 1, "vec3f_normalize");
        vec3f_normalize(values);
    }
    return 0;
}

static int l_vec3f_dist(lua_State *L) {
    float a[3], b[3];
    check_vec3_argument(L, 1, a, "vec3f_dist");
    check_vec3_argument(L, 2, b, "vec3f_dist");
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    lua_pushnumber(L, sqrtf(dx * dx + dy * dy + dz * dz));
    return 1;
}

static int l_collision_find_surface_on_ray(lua_State *L) {
    Vec3f origin, direction, hit_position;
    for (int axis = 0; axis < 3; ++axis) {
        origin[axis] = checked_lua_finite_float(L, axis + 1, "ray origin");
        direction[axis] = checked_lua_finite_float(L, axis + 4, "ray direction");
    }
    float precision = lua_isnoneornil(L, 7) ? 1.0f : checked_lua_finite_float(L, 7, "ray precision");
    if (precision <= 0.0f || precision > 8.0f)
        return luaL_argerror(L, 7, "ray precision must be greater than 0 and at most 8");

    struct Surface *surface = NULL;
    find_surface_on_ray_with_precision(origin, direction, &surface, hit_position, precision);
    lua_newtable(L);
    push_surface(L, surface);
    lua_setfield(L, -2, "surface");
    push_vec3_snapshot(L, hit_position);
    lua_setfield(L, -2, "hitPos");
    return 1;
}

static int l_djui_hud_pointer_unavailable(lua_State *L) {
    if (sCurrentMod && !sCurrentMod->pointer_warned) {
        mod_logf("warn", sCurrentMod->name,
                 "DJUI mouse input has no native pointer source; mouse getters return nil and touchscreen/controller input remains separate");
        sCurrentMod->pointer_warned = true;
    }
    lua_pushnil(L);
    return 1;
}

static int l_djui_hud_pause_menu_created(lua_State *L) {
    extern s16 gMenuMode;
    lua_pushboolean(L, gMenuMode != -1);
    return 1;
}

static int l_hud_hide(lua_State *L) {
    if (lua_gettop(L) != 0) return luaL_error(L, "hud_hide expects no arguments");
    if (!sCurrentMod) return luaL_error(L, "hud_hide() must be called while a mod is executing");
    sHudHiddenOwner = sCurrentMod;
    return 0;
}

static int l_hud_show(lua_State *L) {
    if (lua_gettop(L) != 0) return luaL_error(L, "hud_show expects no arguments");
    if (!sCurrentMod) return luaL_error(L, "hud_show() must be called while a mod is executing");
    sHudHiddenOwner = NULL;
    return 0;
}

static int l_hud_is_hidden(lua_State *L) {
    if (lua_gettop(L) != 0) return luaL_error(L, "hud_is_hidden expects no arguments");
    lua_pushboolean(L, sHudHiddenOwner && sHudHiddenOwner->enabled && sHudHiddenOwner->loaded);
    return 1;
}

static int l_hud_get_value(lua_State *L) {
    int type = (int)luaL_checkinteger(L, 1);
    int value;
    switch (type) {
        case 0: value = gHudDisplay.lives; break;
        case 1: value = gHudDisplay.coins; break;
        case 2: value = gHudDisplay.stars; break;
        case 3: value = gHudDisplay.wedges; break;
        case 4: value = gHudDisplay.keys; break;
        case 5: value = gHudDisplay.flags; break;
        case 6: value = gHudDisplay.timer; break;
        case 7: value = get_hud_camera_status(); break;
        default: return luaL_argerror(L, 1, "expected a HUD_DISPLAY_* value from 0 through 7");
    }
    lua_pushinteger(L, value);
    return 1;
}

static int l_hud_set_value(lua_State *L) {
    int type = (int)luaL_checkinteger(L, 1);
    int value = (int)luaL_checkinteger(L, 2);
    switch (type) {
        case 0: gHudDisplay.lives = (s16)value; break;
        case 1: gHudDisplay.coins = (s16)value; break;
        case 2: gHudDisplay.stars = (s16)value; break;
        case 3: gHudDisplay.wedges = (s16)value; break;
        case 4: gHudDisplay.keys = (s16)value; break;
        case 5: gHudDisplay.flags = (s16)value; break;
        case 6: gHudDisplay.timer = (u16)value; break;
        case 7: set_hud_camera_status((s16)value); break;
        default: return luaL_argerror(L, 1, "expected a HUD_DISPLAY_* value from 0 through 7");
    }
    return 0;
}

bool mod_runtime_hud_hidden(void) {
    return sHudHiddenOwner && sHudHiddenOwner->enabled && sHudHiddenOwner->loaded;
}

static int l_is_game_paused(lua_State *L) {
    extern s16 gMenuMode;
    lua_pushboolean(L, gMenuMode != -1);
    return 1;
}

typedef struct {
    const void *texture;
    int width, height, format, size;
} ModTextureView;

static bool texture_info_view_from_lua(lua_State *L, int index, ModTextureView *view) {
    if (!sCurrentMod) luaL_error(L, "TextureInfo may only be used by its owning active mod");
    if (!lua_istable(L, index)) luaL_argerror(L, index, "expected a TextureInfo table from get_texture_info");
    index = lua_absindex(L, index);
    lua_getfield(L, index, "texture");
    void *texture_pointer = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!texture_pointer) luaL_argerror(L, index, "TextureInfo.texture is invalid");
    for (ModTexture *texture = sCurrentMod->textures; texture; texture = texture->next_owned) {
#ifdef EXTERNAL_DATA
        if (texture_pointer == texture->key) {
#else
        if (texture_pointer == texture->rgba16) {
#endif
            *view = (ModTextureView){ texture_pointer, (int)texture->width,
                (int)texture->height, texture->format, texture->size };
            return true;
        }
    }
#ifdef EXTERNAL_DATA
    lua_getfield(L, index, "name");
    const char *name = lua_tostring(L, -1);
    ModRuntimeNativeTexture native;
    bool found = name && mod_runtime_native_texture_lookup(name, &native) &&
                 texture_pointer == native.asset_path;
    lua_pop(L, 1);
    if (found) {
        *view = (ModTextureView){ texture_pointer, (int)native.width,
            (int)native.height, native.format, native.size };
        return true;
    }
#endif
    luaL_error(L, "TextureInfo does not belong to mod '%s', is not a bundled native texture, or has been unloaded",
               sCurrentMod->name);
    return false;
}

bool mod_runtime_native_texture_lookup(const char *name, ModRuntimeNativeTexture *texture) {
#ifdef EXTERNAL_DATA
    static const struct {
        const char *symbol;
        ModRuntimeNativeTexture texture;
    } native_textures[] = {
        { "coin_seg3_texture_03005780", { "actors/coin/coin_front.ia16", 32, 32, G_IM_FMT_IA, G_IM_SIZ_16b } },
        { "coin_seg3_texture_03005F80", { "actors/coin/coin_tilt_right.ia16", 32, 32, G_IM_FMT_IA, G_IM_SIZ_16b } },
        { "coin_seg3_texture_03006780", { "actors/coin/coin_side.ia16", 32, 32, G_IM_FMT_IA, G_IM_SIZ_16b } },
        { "coin_seg3_texture_03006F80", { "actors/coin/coin_tilt_left.ia16", 32, 32, G_IM_FMT_IA, G_IM_SIZ_16b } },
        { "sparkles_seg4_texture_04027490", { "actors/sparkle/sparkle_0.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "sparkles_seg4_texture_04027C90", { "actors/sparkle/sparkle_1.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "sparkles_seg4_texture_04028490", { "actors/sparkle/sparkle_2.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "sparkles_seg4_texture_04028C90", { "actors/sparkle/sparkle_3.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "sparkles_seg4_texture_04029490", { "actors/sparkle/sparkle_4.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "sparkles_seg4_texture_04029C90", { "actors/sparkle/sparkle_5.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "texture_hud_char_star", { "textures/segment2/segment2.05C00.rgba16", 16, 16, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_0", { "textures/intro_raw/red_star_0.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_1", { "textures/intro_raw/red_star_1.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_2", { "textures/intro_raw/red_star_2.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_3", { "textures/intro_raw/red_star_3.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_4", { "textures/intro_raw/red_star_4.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_5", { "textures/intro_raw/red_star_5.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_6", { "textures/intro_raw/red_star_6.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_red_star_7", { "textures/intro_raw/red_star_7.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_0", { "textures/intro_raw/white_star_0.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_1", { "textures/intro_raw/white_star_1.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_2", { "textures/intro_raw/white_star_2.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_3", { "textures/intro_raw/white_star_3.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_4", { "textures/intro_raw/white_star_4.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_5", { "textures/intro_raw/white_star_5.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_6", { "textures/intro_raw/white_star_6.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
        { "gd_texture_white_star_7", { "textures/intro_raw/white_star_7.rgba16", 32, 32, G_IM_FMT_RGBA, G_IM_SIZ_16b } },
    };
    if (!name || !texture) return false;
    for (size_t i = 0; i < sizeof(native_textures) / sizeof(native_textures[0]); ++i) {
        if (strcmp(name, native_textures[i].symbol) == 0) {
            *texture = native_textures[i].texture;
            return true;
        }
    }
#else
    (void)name;
    (void)texture;
#endif
    return false;
}

static int l_get_texture_info(lua_State *L) {
    size_t length = 0;
    const char *name = luaL_checklstring(L, 1, &length);
    if (!length || length > 240 || memchr(name, '\0', length))
        return luaL_error(L, "get_texture_info: expected a non-empty texture name of at most 240 bytes");
    if (!sCurrentMod) return luaL_error(L, "get_texture_info() must be called while a mod is executing");
    char requested[241];
    memcpy(requested, name, length);
    requested[length] = '\0';
    ModTexture *texture = find_mod_texture(sCurrentMod, requested);
    if (!texture) {
#ifdef EXTERNAL_DATA
        ModRuntimeNativeTexture native;
        if (mod_runtime_native_texture_lookup(requested, &native)) {
            lua_newtable(L);
            lua_pushlightuserdata(L, (void *)native.asset_path);
            lua_setfield(L, -2, "texture");
            lua_pushinteger(L, native.width); lua_setfield(L, -2, "width");
            lua_pushinteger(L, native.height); lua_setfield(L, -2, "height");
            lua_pushinteger(L, native.format); lua_setfield(L, -2, "format");
            lua_pushinteger(L, native.size); lua_setfield(L, -2, "size");
            lua_pushstring(L, requested); lua_setfield(L, -2, "name");
            return 1;
        }
#endif
        if (!sCurrentMod->texture_lookup_warned) {
            mod_logf("warn", sCurrentMod->name,
                     "texture '%s' was not found in this mod's active DynOS .tex resources or the bundled native texture catalog",
                     requested);
            sCurrentMod->texture_lookup_warned = true;
        }
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushlightuserdata(L,
#ifdef EXTERNAL_DATA
                          texture->key
#else
                          texture->rgba16
#endif
    );
    lua_setfield(L, -2, "texture");
    lua_pushinteger(L, texture->width); lua_setfield(L, -2, "width");
    lua_pushinteger(L, texture->height); lua_setfield(L, -2, "height");
    lua_pushinteger(L, texture->format); lua_setfield(L, -2, "format");
    lua_pushinteger(L, texture->size); lua_setfield(L, -2, "size");
    lua_pushstring(L, texture->name); lua_setfield(L, -2, "name");
    return 1;
}

static int l_djui_hud_render_texture(lua_State *L) {
    ModTextureView texture;
    texture_info_view_from_lua(L, 1, &texture);
    float x = checked_lua_finite_float(L, 2, "djui_hud_render_texture");
    float y = checked_lua_finite_float(L, 3, "djui_hud_render_texture");
    float scale_x = checked_lua_finite_float(L, 4, "djui_hud_render_texture");
    float scale_y = checked_lua_finite_float(L, 5, "djui_hud_render_texture");
    if (scale_x <= 0.0f || scale_y <= 0.0f ||
        (float)texture.width * scale_x > 8192.0f ||
        (float)texture.height * scale_y > 8192.0f)
        return luaL_argerror(L, 4, "texture scale must be positive and produce dimensions no larger than 8192 pixels");
    if (!mod_hud_render_texture(texture.texture, texture.width, texture.height,
                                texture.format, texture.size, x, y, scale_x, scale_y))
        return luaL_error(L, "djui_hud_render_texture: rotated textures, invalid dimensions, or a full HUD command queue are unsupported");
    return 0;
}

/* CoopDX's HUD interpolation is tied to display-list frame interpolation,
 * which this renderer does not currently expose. Preserve the original Lua
 * signature and draw the requested current-frame endpoint; report the
 * missing smoothing once per mod instead of silently claiming interpolation. */
static int l_djui_hud_render_texture_interpolated(lua_State *L) {
    ModTextureView texture;
    texture_info_view_from_lua(L, 1, &texture);
    (void)checked_lua_finite_float(L, 2, "djui_hud_render_texture_interpolated");
    (void)checked_lua_finite_float(L, 3, "djui_hud_render_texture_interpolated");
    float previous_scale_x = checked_lua_finite_float(L, 4, "djui_hud_render_texture_interpolated");
    float previous_scale_y = checked_lua_finite_float(L, 5, "djui_hud_render_texture_interpolated");
    float x = checked_lua_finite_float(L, 6, "djui_hud_render_texture_interpolated");
    float y = checked_lua_finite_float(L, 7, "djui_hud_render_texture_interpolated");
    float scale_x = checked_lua_finite_float(L, 8, "djui_hud_render_texture_interpolated");
    float scale_y = checked_lua_finite_float(L, 9, "djui_hud_render_texture_interpolated");
    if (previous_scale_x <= 0.0f || previous_scale_y <= 0.0f ||
        scale_x <= 0.0f || scale_y <= 0.0f ||
        (float)texture.width * previous_scale_x > 8192.0f ||
        (float)texture.height * previous_scale_y > 8192.0f ||
        (float)texture.width * scale_x > 8192.0f ||
        (float)texture.height * scale_y > 8192.0f)
        return luaL_argerror(L, 4, "interpolated texture scales must be positive and produce dimensions no larger than 8192 pixels");
    if (sCurrentMod && !sCurrentMod->hud_interpolation_warned) {
        mod_log("warning", sCurrentMod->name,
                "djui_hud_render_texture_interpolated keeps the current-frame endpoint; this renderer has no display-list frame interpolation");
        sCurrentMod->hud_interpolation_warned = true;
    }
    if (!mod_hud_render_texture(texture.texture, texture.width, texture.height,
                                texture.format, texture.size, x, y, scale_x, scale_y))
        return luaL_error(L, "djui_hud_render_texture_interpolated: unsupported texture dimensions or full HUD command queue");
    return 0;
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

static struct MarioState *checked_mario_argument(lua_State *L, int index) {
    LuaUserdata *ud = luaL_checkudata(L, index, "sm64.mod.MarioState");
    if (ud->kind != LUA_UD_MARIO) luaL_error(L, "expected a MarioState argument");
    if (ud->field != 0) luaL_error(L, "MarioState[%d] has no native local player in single-player mode", ud->field);
    struct MarioState *m = current_mario();
    if (!m) luaL_error(L, "Mario state is unavailable");
    return m;
}

static int l_level_trigger_warp(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_Integer operation = luaL_checkinteger(L, 2);
    if (operation < INT32_MIN || operation > INT32_MAX)
        return luaL_argerror(L, 2, "warp operation must fit a signed 32-bit integer");
    lua_pushinteger(L, level_trigger_warp(m, (s32)operation));
    return 1;
}

static int l_perform_ground_step(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_pushinteger(L, perform_ground_step(m));
    return 1;
}

static int l_stationary_ground_step(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_pushinteger(L, stationary_ground_step(m));
    return 1;
}

static int l_perform_air_step(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    uint32_t step_arg = (uint32_t)luaL_optinteger(L, 2, 0);
    lua_pushinteger(L, perform_air_step(m, step_arg));
    return 1;
}

static int l_mario_bonk_reflection(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    /* CoopDX declares this parameter u8. Preserve its narrowing semantics. */
    u8 negate_speed = (u8)luaL_checkinteger(L, 2);
    mario_bonk_reflection(m, negate_speed);
    return 0;
}

static int l_mario_get_floor_class(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_pushinteger(L, mario_get_floor_class(m));
    return 1;
}

static int l_perform_water_step(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_pushinteger(L, perform_water_step(m));
    return 1;
}

static int l_mario_grab_used_object(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    if (!m->usedObj) return luaL_error(L, "mario_grab_used_object requires MarioState.usedObj to be set");
    mario_grab_used_object(m);
    return 0;
}

static int l_mario_stop_riding_object(lua_State *L) {
    mario_stop_riding_object(checked_mario_argument(L, 1));
    return 0;
}

static int l_mario_stop_riding_and_holding(lua_State *L) {
    mario_stop_riding_and_holding(checked_mario_argument(L, 1));
    return 0;
}

static int l_mario_set_bubbled_unsupported(lua_State *L) {
    (void)checked_mario_argument(L, 1);
    return luaL_error(L,
        "unsupported CoopDX API 'mario_set_bubbled': this engine has no ACT_BUBBLED action or multiplayer bubble/life/camera system");
}

static int l_mario_can_bubble(lua_State *L) {
    if (!lua_isnoneornil(L, 1)) (void)checked_mario_argument(L, 1);
    /* CoopDX returns false when no other active player can leave the bubble. */
    if (sCurrentMod && !sCurrentMod->bubble_api_warned) {
        sCurrentMod->bubble_api_warned = true;
        mod_log("warning", sCurrentMod->name,
                "mario_can_bubble is false in this single-player runtime; no multiplayer bubble action is available");
    }
    lua_pushboolean(L, false);
    return 1;
}

static int l_interact_coin(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    /* CoopDX accepts the legacy interactType argument but fixes it to INTERACT_COIN. */
    (void)luaL_checkinteger(L, 2);
    struct Object *object = checked_object(L, 3);
    lua_pushinteger(L, process_interaction(m, INTERACT_COIN, object, interact_coin));
    return 1;
}

static int l_audio_sample_load(lua_State *L) {
    size_t length;
    const char *path = luaL_checklstring(L, 1, &length);
    if (!sCurrentMod) return luaL_error(L, "audio_sample_load requires an active mod");
    if (length > 240 || memchr(path, 0, length) || !safe_relative_path(path))
        return luaL_argerror(L, 1, "expected a mod-relative sample path of at most 240 bytes");

    char resolved[241];
    if (!mod_resolve_resource_path(sCurrentMod, path, resolved)) {
        mod_logf("error", sCurrentMod->name,
                 "audio_sample_load('%s'): resource path is missing or its basename is ambiguous", path);
        lua_pushnil(L);
        return 1;
    }
    const char *error;
    uint32_t id = mod_audio_load(sCurrentMod, sCurrentMod->path, resolved, &error);
    if (!id) {
        mod_logf("error", sCurrentMod->name, "audio_sample_load('%s'): %s", path, error);
        lua_pushnil(L);
        return 1;
    }
    uint32_t *handle = lua_newuserdata(L, sizeof(*handle));
    *handle = id;
    luaL_getmetatable(L, "sm64.mod.AudioSample");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_audio_sample_play(lua_State *L) {
    uint32_t *handle = luaL_checkudata(L, 1, "sm64.mod.AudioSample");
    float position[3];
    LuaUserdata *vector = luaL_testudata(L, 2, "sm64.mod.Vec3f");
    if (vector) {
        float *coords = vec_f_pointer(vector);
        if (!coords) return luaL_argerror(L, 2, "stale sound position");
        memcpy(position, coords, sizeof(position));
    } else {
        luaL_checktype(L, 2, LUA_TTABLE);
        for (int axis = 0; axis < 3; ++axis) {
            char key[2] = { "xyz"[axis], 0 };
            lua_getfield(L, 2, key);
            position[axis] = checked_lua_finite_float(L, -1, key);
            lua_pop(L, 1);
        }
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!isfinite(position[axis])) return luaL_argerror(L, 2, "sound position must be finite");
    float gain = checked_lua_finite_float(L, 3, "sample volume");
    if (gain < 0 || gain > 4) return luaL_argerror(L, 3, "sample volume must be in 0..4");
    float pan = 0;
    if (gCamera) {
        /* The stock camera has no stored CoopDX view matrix. Derive the
         * horizontal basis from the native camera's position and focus. */
        double dx = (double)position[0] - gCamera->pos[0];
        double dy = (double)position[1] - gCamera->pos[1];
        double dz = (double)position[2] - gCamera->pos[2];
        double fx = (double)gCamera->focus[0] - gCamera->pos[0];
        double fz = (double)gCamera->focus[2] - gCamera->pos[2];
        double facing = hypot(fx, fz);
        if (facing > 0)
            pan = 2 * get_sound_pan((float)((-fz * dx + fx * dz) / facing * 10),
                                     (float)((fx * dx + fz * dz) / facing * 10)) - 1;
        if (!isfinite(pan)) pan = 0;
        pan = fmaxf(-1, fminf(pan, 1));
        gain *= (float)fmax(0, 1 - sqrt(dx * dx + dy * dy + dz * dz) / 32000);
    }
    const char *error;
    if (!mod_audio_play(sCurrentMod, *handle, gain, pan, &error))
        return luaL_error(L, "audio_sample_play: %s", error);
    return 0;
}

static int l_audio_sample_stop(lua_State *L) {
    uint32_t *handle = luaL_checkudata(L, 1, "sm64.mod.AudioSample");
    if (!mod_audio_stop(sCurrentMod, *handle))
        return luaL_error(L, "audio_sample_stop: sample is stale or belongs to another mod");
    return 0;
}

static int l_audio_sample_destroy(lua_State *L) {
    uint32_t *handle = luaL_checkudata(L, 1, "sm64.mod.AudioSample");
    if (!mod_audio_destroy(sCurrentMod, *handle))
        return luaL_error(L, "audio_sample_destroy: sample is stale or belongs to another mod");
    return 0;
}

static int l_audio_stream_load(lua_State *L) {
    size_t length;
    const char *path = luaL_checklstring(L, 1, &length);
    if (!sCurrentMod) return luaL_error(L, "audio_stream_load requires an active mod");
    if (length > 240 || memchr(path, 0, length) || !safe_relative_path(path))
        return luaL_argerror(L, 1, "expected a mod-relative audio path of at most 240 bytes");
    char resolved[241];
    if (!mod_resolve_resource_path(sCurrentMod, path, resolved)) {
        mod_logf("error", sCurrentMod->name,
                 "audio_stream_load('%s'): resource path is missing or its basename is ambiguous", path);
        lua_pushnil(L);
        return 1;
    }
    const char *error;
    uint32_t id = mod_audio_load_stream(sCurrentMod, sCurrentMod->path, resolved, &error);
    if (!id) {
        mod_logf("error", sCurrentMod->name, "audio_stream_load('%s'): %s", path, error);
        lua_pushnil(L);
        return 1;
    }
    mod_logf("info", sCurrentMod->name, "audio_stream_load('%s'): loaded bounded %s stream",
             path, file_has_suffix(resolved, ".mp3") ? "MP3" : "Ogg Vorbis");
    uint32_t *handle = lua_newuserdata(L, sizeof(*handle));
    *handle = id;
    luaL_getmetatable(L, "sm64.mod.AudioStream");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_audio_stream_play(lua_State *L) {
    uint32_t *handle = luaL_checkudata(L, 1, "sm64.mod.AudioStream");
    bool restart = lua_toboolean(L, 2);
    float volume = checked_lua_finite_float(L, 3, "audio_stream_play");
    if (volume < 0 || volume > 4)
        return luaL_argerror(L, 3, "audio stream volume must be in 0..4");
    const char *error;
    if (!mod_audio_stream_play(sCurrentMod, *handle, restart, volume, &error))
        return luaL_error(L, "audio_stream_play: %s", error);
    return 0;
}

static int l_audio_stream_set_looping(lua_State *L) {
    uint32_t *handle = luaL_checkudata(L, 1, "sm64.mod.AudioStream");
    if (!mod_audio_stream_set_looping(sCurrentMod, *handle, lua_toboolean(L, 2)))
        return luaL_error(L, "audio_stream_set_looping: stream is stale or belongs to another mod");
    return 0;
}

static int l_audio_stream_stop(lua_State *L) {
    uint32_t *handle = luaL_checkudata(L, 1, "sm64.mod.AudioStream");
    if (!mod_audio_stream_stop(sCurrentMod, *handle))
        return luaL_error(L, "audio_stream_stop: stream is stale or belongs to another mod");
    return 0;
}

static int l_audio_stream_destroy(lua_State *L) {
    uint32_t *handle = luaL_checkudata(L, 1, "sm64.mod.AudioStream");
    if (!mod_audio_destroy(sCurrentMod, *handle))
        return luaL_error(L, "audio_stream_destroy: stream is stale or belongs to another mod");
    return 0;
}

/* CoopDX-compatible sequence override. Assets are resolved only inside the
 * active mod's sandbox; the upstream short name maps to sound/<name>.m64. */
static int l_smlua_audio_utils_replace_sequence(lua_State *L) {
    lua_Integer sequence_id = luaL_checkinteger(L, 1);
    lua_Integer bank_id = luaL_checkinteger(L, 2);
    lua_Integer default_volume = luaL_checkinteger(L, 3);
    const char *name = luaL_checkstring(L, 4);
    if (!sCurrentMod)
        return luaL_error(L, "smlua_audio_utils_replace_sequence requires an active mod");
    if (sequence_id < 0 || sequence_id > UINT8_MAX || bank_id < 0 || bank_id >= 64 ||
        default_volume < 0 || default_volume > UINT8_MAX)
        return luaL_error(L, "smlua_audio_utils_replace_sequence arguments are out of range");
    if (!safe_relative_path(name) || strchr(name, '/') || strchr(name, '\\') ||
        strlen(name) > 230)
        return luaL_error(L, "sequence name must be a safe mod-local basename");
    char relative_path[241];
    int written = snprintf(relative_path, sizeof(relative_path), "sound/%s.m64", name);
    if (written <= 0 || (size_t)written >= sizeof(relative_path))
        return luaL_error(L, "sequence name is too long");
    char resolved[241];
    if (!mod_resolve_resource_path(sCurrentMod, relative_path, resolved))
        return luaL_error(L, "smlua_audio_utils_replace_sequence: missing mod-local %s", relative_path);
    const char *error = NULL;
    if (!mod_audio_sequence_override_load(sCurrentMod, (uint8_t)sequence_id,
            (uint8_t)bank_id, (uint8_t)default_volume, sCurrentMod->path, resolved, &error))
        return luaL_error(L, "smlua_audio_utils_replace_sequence('%s'): %s",
                          relative_path, error ? error : "registration failed");
    audio_mod_sequence_set_default_volume((u32)sequence_id, (u8)default_volume);
    mod_logf("info", sCurrentMod->name,
             "registered sequence override %u using bank %u and %s",
             (unsigned)sequence_id, (unsigned)bank_id, relative_path);
    return 0;
}

static int l_play_sound(lua_State *L) {
    s32 sound_bits = (s32)luaL_checkinteger(L, 1);
    LuaUserdata *position = luaL_checkudata(L, 2, "sm64.mod.Vec3f");
    if (position->kind != LUA_UD_VEC_F) return luaL_argerror(L, 2, "expected a Vec3f position");
    float *position_values = vec_f_pointer(position);
    if (!position_values) return luaL_error(L, "sound position is unavailable");
    play_sound(sound_bits, position_values);
    return 0;
}

static int l_play_mario_heavy_landing_sound(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    u32 sound_bits = (u32)luaL_checkinteger(L, 2);
    play_mario_heavy_landing_sound(m, sound_bits);
    return 0;
}

static int l_play_mario_action_sound(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    u32 sound_bits = (u32)luaL_checkinteger(L, 2);
    u32 wave_particle_type = (u32)luaL_checkinteger(L, 3);
    play_mario_action_sound(m, sound_bits, wave_particle_type);
    return 0;
}

static uint32_t mario_character_sound_bits(lua_Integer character_sound);

/* CoopDX uses CHAR_SOUND_* indices for the secondary argument while this
 * port's legacy helper expects packed sound bits. Preserve the upstream
 * behavior at the adapter boundary instead of passing those enum values into
 * the legacy sound bank. */
static int l_play_mario_sound(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_Integer primary = luaL_checkinteger(L, 2);
    lua_Integer secondary = luaL_checkinteger(L, 3);
    if (primary < INT32_MIN || primary > INT32_MAX)
        return luaL_argerror(L, 2, "primary sound bits must fit in s32");
    if (secondary < INT32_MIN || secondary > INT32_MAX)
        return luaL_argerror(L, 3, "secondary sound bits must fit in s32");
    if (primary == SOUND_ACTION_TERRAIN_JUMP) {
        play_mario_action_sound(m,
            (m->flags & MARIO_METAL_CAP) ? (s32)SOUND_ACTION_METAL_JUMP
                                          : (s32)SOUND_ACTION_TERRAIN_JUMP,
            1);
    } else {
        play_sound_if_no_flag(m, (u32)(s32)primary, MARIO_ACTION_SOUND_PLAYED);
    }
    if (secondary == 0) play_mario_jump_sound(m);
    if (secondary != -1 && !(m->flags & MARIO_MARIO_SOUND_PLAYED)) {
        uint32_t sound_bits = mario_character_sound_bits(secondary);
        if (sound_bits) {
            if (sCurrentMod && !sCurrentMod->character_sound_warned) {
                sCurrentMod->character_sound_warned = true;
                mod_log("warning", sCurrentMod->name,
                        "Character Select sound overrides and HOOK_CHARACTER_SOUND are unavailable; using the native Mario sound bank");
            }
            float *position = m->marioObj ? m->marioObj->header.gfx.cameraToObject : gGlobalSoundSource;
            play_sound((s32)sound_bits, position);
        }
        m->flags |= MARIO_MARIO_SOUND_PLAYED;
    }
    return 0;
}

static int l_play_course_clear(lua_State *L) {
    (void)L;
    play_course_clear();
    return 0;
}

static int l_play_flip_sounds(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_Integer frames[3];
    for (int i = 0; i < 3; ++i) {
        frames[i] = luaL_checkinteger(L, i + 2);
        if (frames[i] < INT16_MIN || frames[i] > INT16_MAX)
            return luaL_argerror(L, i + 2, "animation frame must fit in s16");
    }
    play_flip_sounds(m, (s16)frames[0], (s16)frames[1], (s16)frames[2]);
    return 0;
}

static int l_set_camera_mode(lua_State *L) {
    struct Camera *camera = checked_camera_userdata(L, 1);
    lua_Integer mode = luaL_checkinteger(L, 2);
    lua_Integer frames = luaL_checkinteger(L, 3);
    if (mode < INT16_MIN || mode > INT16_MAX)
        return luaL_argerror(L, 2, "camera mode must fit in s16");
    if (frames < INT16_MIN || frames > INT16_MAX)
        return luaL_argerror(L, 3, "transition frames must fit in s16");
    set_camera_mode(camera, (s16)mode, (s16)frames);
    return 0;
}

static int l_set_camera_shake_from_hit(lua_State *L) {
    lua_Integer shake = luaL_checkinteger(L, 1);
    if (shake < INT16_MIN || shake > INT16_MAX)
        return luaL_argerror(L, 1, "camera shake type must fit in s16");
    set_camera_shake_from_hit((s16)shake);
    return 0;
}

static int l_set_camera_shake_from_point(lua_State *L) {
    lua_Integer shake = luaL_checkinteger(L, 1);
    if (shake < SHAKE_POS_SMALL || shake > SHAKE_POS_BOWLING_BALL)
        return luaL_argerror(L, 1, "camera point shake must be a SHAKE_POS_* constant");
    f32 x = checked_lua_finite_float(L, 2, "camera shake x");
    f32 y = checked_lua_finite_float(L, 3, "camera shake y");
    f32 z = checked_lua_finite_float(L, 4, "camera shake z");
    set_camera_shake_from_point((s16)shake, x, y, z);
    return 0;
}

static int l_play_transition(lua_State *L) {
    lua_Integer type = luaL_checkinteger(L, 1);
    lua_Integer time = luaL_checkinteger(L, 2);
    if (type < INT16_MIN || type > INT16_MAX)
        return luaL_argerror(L, 1, "transition type must fit in s16");
    if (time < INT16_MIN || time > INT16_MAX)
        return luaL_argerror(L, 2, "transition time must fit in s16");
    u8 red = (u8)luaL_checkinteger(L, 3);
    u8 green = (u8)luaL_checkinteger(L, 4);
    u8 blue = (u8)luaL_checkinteger(L, 5);
    play_transition((s16)type, (s16)time, red, green, blue);
    return 0;
}

static int l_play_cap_music(lua_State *L) {
    u16 sequence_args = (u16)luaL_checkinteger(L, 1);
    play_cap_music(sequence_args);
    return 0;
}

static int l_play_shell_music(lua_State *L) {
    (void)L;
    play_shell_music();
    return 0;
}

static int l_stop_shell_music(lua_State *L) {
    (void)L;
    stop_shell_music();
    return 0;
}

static uint32_t mario_character_sound_bits(lua_Integer character_sound) {
    static const uint32_t sound_bits[] = {
        SOUND_MARIO_YAH_WAH_HOO, SOUND_MARIO_HOOHOO, SOUND_MARIO_YAHOO,
        SOUND_MARIO_UH, SOUND_MARIO_HRMM, SOUND_MARIO_WAH2, SOUND_MARIO_WHOA,
        SOUND_MARIO_EEUH, SOUND_MARIO_ATTACKED, SOUND_MARIO_OOOF, SOUND_MARIO_OOOF2,
        SOUND_MARIO_HERE_WE_GO, SOUND_MARIO_YAWNING, SOUND_MARIO_SNORING1,
        SOUND_MARIO_SNORING2, SOUND_MARIO_WAAAOOOW, SOUND_MARIO_HAHA,
        SOUND_MARIO_HAHA_2, SOUND_MARIO_UH2, SOUND_MARIO_UH2_2,
        SOUND_MARIO_ON_FIRE, SOUND_MARIO_DYING, SOUND_MARIO_PANTING_COLD,
        SOUND_MARIO_PANTING, SOUND_MARIO_COUGHING1, SOUND_MARIO_COUGHING2,
        SOUND_MARIO_COUGHING3, SOUND_MARIO_PUNCH_YAH, SOUND_MARIO_PUNCH_HOO,
        SOUND_MARIO_MAMA_MIA, SOUND_MARIO_GROUND_POUND_WAH, SOUND_MARIO_DROWNING,
        SOUND_MARIO_PUNCH_WAH, SOUND_MARIO_YAHOO_WAHA_YIPPEE, SOUND_MARIO_DOH,
        SOUND_MARIO_GAME_OVER, SOUND_MARIO_HELLO, SOUND_MARIO_PRESS_START_TO_PLAY,
        SOUND_MARIO_TWIRL_BOUNCE, SOUND_MARIO_SNORING3, SOUND_MARIO_SO_LONGA_BOWSER,
        SOUND_MARIO_IMA_TIRED, 0, SOUND_MARIO_OKEY_DOKEY
    };
    if (character_sound < 0 || (size_t)character_sound >= sizeof(sound_bits) / sizeof(sound_bits[0]))
        return 0;
    return sound_bits[character_sound];
}

static int play_character_sound_from_lua(lua_State *L, bool has_offset, bool has_flags) {
    struct MarioState *m = lua_isnoneornil(L, 1) ? NULL : checked_mario_argument(L, 1);
    lua_Integer character_sound = luaL_checkinteger(L, 2);
    lua_Integer third_argument = (has_offset || has_flags) ? luaL_checkinteger(L, 3) : 0;
    lua_Integer offset = has_offset ? third_argument : 0;
    u32 flags = has_flags ? (u32)third_argument : 0;
    if (offset < 0 || (uint64_t)offset > UINT32_MAX)
        return luaL_argerror(L, 3, "offset must fit in u32");
    uint32_t sound_bits = mario_character_sound_bits(character_sound);
    if (!m || (m->flags & flags) != 0) return 0;

    if (sound_bits) {
        if (sCurrentMod && !sCurrentMod->character_sound_warned) {
            sCurrentMod->character_sound_warned = true;
            mod_log("warning", sCurrentMod->name,
                    "Character Select sound overrides and HOOK_CHARACTER_SOUND are unavailable; using the native Mario sound bank");
        }
        float *position = m->marioObj ? m->marioObj->header.gfx.cameraToObject : gGlobalSoundSource;
        play_sound((s32)(sound_bits + (uint32_t)offset), position);
    }
    m->flags |= flags;
    return 0;
}

static int l_play_character_sound(lua_State *L) {
    return play_character_sound_from_lua(L, false, false);
}

static int l_play_character_sound_offset(lua_State *L) {
    return play_character_sound_from_lua(L, true, false);
}

static int l_play_character_sound_if_no_flag(lua_State *L) {
    return play_character_sound_from_lua(L, false, true);
}

/* CoopDX's server API accepts an Object and reliability flag, then only emits
 * a packet when a multiplayer sync object exists. In this single-player
 * runtime there is no packet transport; preserve the native argument checks
 * and return count, while clearly reporting that the operation is inactive. */
static int l_network_send_object_single_player(lua_State *L) {
    if (lua_gettop(L) != 2) return luaL_error(L, "network_send_object expects an Object and reliability boolean");
    (void)checked_object(L, 1);
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    if (sCurrentMod && !sCurrentMod->network_send_warned) {
        sCurrentMod->network_send_warned = true;
        mod_log("warning", sCurrentMod->name,
                "network_send_object validated locally; multiplayer synchronization is unavailable in this single-player build");
    }
    return 0;
}

static int l_network_send_unsupported(lua_State *L) {
    (void)L;
    return luaL_error(L,
        "unsupported CoopDX API 'network_send': this single-player runtime has no packet transport or HOOK_ON_PACKET_RECEIVE dispatch");
}

/* This runtime has no lobby transport. In its offline game loop the sole
 * local player owns world state, so server-authority checks are true while
 * packet synchronization remains explicitly unavailable. */
static int l_network_is_server(lua_State *L) {
    if (lua_gettop(L) != 0) return luaL_error(L, "network_is_server expects no arguments");
    if (sCurrentMod && !sCurrentMod->multiplayer_settings_warned) {
        sCurrentMod->multiplayer_settings_warned = true;
        mod_log("warning", sCurrentMod->name,
                "network_is_server is true for the sole offline authority; multiplayer transport and synchronization are unavailable");
    }
    lua_pushboolean(L, true);
    return 1;
}

static int l_network_index_single_player(lua_State *L) {
    if (lua_gettop(L) != 1) return luaL_error(L, "%s expects one player index", lua_tostring(L, lua_upvalueindex(1)));
    lua_Integer index = luaL_checkinteger(L, 1);
    if (index < 0 || index > UINT8_MAX) return luaL_argerror(L, 1, "player index must fit in u8");
    if (index == 0) { lua_pushinteger(L, 0); return 1; }
    if (sCurrentMod && !sCurrentMod->network_send_warned) {
        sCurrentMod->network_send_warned = true;
        mod_log("warning", sCurrentMod->name,
                "network index conversion only maps the sole local player (index 0); other players return UNKNOWN_GLOBAL_INDEX (255)");
    }
    lua_pushinteger(L, UINT8_MAX);
    return 1;
}

static int l_network_get_player_text_color_string(lua_State *L) {
    if (lua_gettop(L) != 1)
        return luaL_error(L, "network_get_player_text_color_string expects one local player index");
    lua_Integer index = luaL_checkinteger(L, 1);
    if (index != 0)
        return luaL_argerror(L, 1, "only local player index 0 exists in this single-player runtime");
    if (sCurrentMod && !sCurrentMod->network_text_color_warned) {
        sCurrentMod->network_text_color_warned = true;
        mod_log("warning", sCurrentMod->name,
                "network player color metadata is unavailable; local player names use the neutral white text prefix");
    }
    lua_pushliteral(L, "\\#ffffff\\");
    return 1;
}

static int l_mario_set_forward_vel(lua_State *L) {
    struct MarioState *m = current_mario();
    if (!m) return 0;
    m->forwardVel = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_set_mario_animation(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_pushinteger(L, set_mario_animation(m, (s32)luaL_checkinteger(L, 2)));
    return 1;
}

static int l_set_mario_anim_with_accel(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_Integer target = luaL_checkinteger(L, 2);
    lua_Integer accel = luaL_checkinteger(L, 3);
    if (target < INT32_MIN || target > INT32_MAX)
        return luaL_argerror(L, 2, "targetAnimID must fit in s32");
    if (accel < INT32_MIN || accel > INT32_MAX)
        return luaL_argerror(L, 3, "accel must fit in s32");
    lua_pushinteger(L, set_mario_anim_with_accel(m, (s32)target, (s32)accel));
    return 1;
}

static int l_set_character_anim_with_accel(lua_State *L) {
    struct MarioState *m = checked_mario_argument(L, 1);
    lua_Integer character_anim = luaL_checkinteger(L, 2);
    lua_Integer accel = luaL_checkinteger(L, 3);
    if (character_anim < INT32_MIN || character_anim > INT32_MAX)
        return luaL_argerror(L, 2, "characterAnim must fit in s32");
    if (accel < INT32_MIN || accel > INT32_MAX)
        return luaL_argerror(L, 3, "accel must fit in s32");
    if (sCurrentMod && !sCurrentMod->character_animation_warned) {
        sCurrentMod->character_animation_warned = true;
        mod_log("warning", sCurrentMod->name,
                "Character Select animation remapping is unavailable; using the native Mario animation ID");
    }
    /* This port has no Character Select animation map. CoopDX falls back to
     * the same animation ID for its default Mario character, so preserve that
     * path while making the missing alternate-character mapping explicit. */
    lua_pushinteger(L, set_mario_anim_with_accel(m, (s32)character_anim, (s32)accel));
    return 1;
}

static int l_set_anim_to_frame(lua_State *L) {
    lua_Integer frame = luaL_checkinteger(L, 2);
    if (frame < INT16_MIN || frame > INT16_MAX)
        return luaL_argerror(L, 2, "animation frame must fit in s16");
    struct MarioState *m = lua_isnoneornil(L, 1) ? NULL : checked_mario_argument(L, 1);
    if (m && m->marioObj) set_anim_to_frame(m, (s16)frame);
    return 0;
}

static ModAnimation *find_custom_animation(const char *name) {
    for (ModAnimation *animation = sAnimations; animation; animation = animation->next_global) {
        if (animation->owner->enabled && strcmp(animation->name, name) == 0) return animation;
    }
    return NULL;
}

static int l_smlua_anim_util_register_animation(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    lua_Integer flags = luaL_checkinteger(L, 2);
    lua_Integer y_divisor = luaL_checkinteger(L, 3);
    lua_Integer start_frame = luaL_checkinteger(L, 4);
    lua_Integer loop_start = luaL_checkinteger(L, 5);
    lua_Integer loop_end = luaL_checkinteger(L, 6);
    luaL_checktype(L, 7, LUA_TTABLE);
    luaL_checktype(L, 8, LUA_TTABLE);
    if (!sCurrentMod) return luaL_error(L, "smlua_anim_util_register_animation must be called while loading a mod");
    if (!name[0] || strlen(name) >= MOD_MAX_NAME)
        return luaL_argerror(L, 1, "animation name must contain 1..127 bytes");
    if (flags < INT16_MIN || flags > INT16_MAX)
        return luaL_argerror(L, 2, "flags must fit in s16");
    if (y_divisor < INT16_MIN || y_divisor > INT16_MAX)
        return luaL_argerror(L, 3, "animYTransDivisor must fit in s16");
    if (start_frame < 0 || start_frame > INT16_MAX || loop_start < 0 || loop_start > INT16_MAX ||
        loop_end < 0 || loop_end > INT16_MAX || loop_start > loop_end || start_frame > loop_end)
        return luaL_error(L, "animation frame range must satisfy 0 <= loopStart <= loopEnd and 0 <= startFrame <= loopEnd");
    if (find_custom_animation(name)) {
        mod_logf("warning", sCurrentMod->name,
                 "smlua_anim_util_register_animation('%s'): name is already registered; keeping the existing animation",
                 name);
        return 0;
    }

    size_t values_length = (size_t)lua_rawlen(L, 7);
    size_t index_length = (size_t)lua_rawlen(L, 8);
    if (!values_length || values_length > MOD_MAX_CUSTOM_ANIMATION_ARRAY_ELEMENTS ||
        !index_length || index_length > MOD_MAX_CUSTOM_ANIMATION_ARRAY_ELEMENTS ||
        index_length % 6 != 0)
        return luaL_error(L, "animation values/index tables have invalid lengths (index length must be a positive multiple of 6)");
    size_t array_bytes = (values_length + index_length) * sizeof(u16);
    size_t name_bytes = strlen(name) + 1;
    if (array_bytes > MOD_MAX_CUSTOM_ANIMATION_BYTES ||
        sizeof(ModAnimation) + sizeof(struct Animation) + name_bytes > MOD_MAX_CUSTOM_ANIMATION_BYTES - array_bytes)
        return luaL_error(L, "animation '%s' exceeds the per-mod animation memory limit", name);
    size_t allocation_bytes = array_bytes + sizeof(ModAnimation) + sizeof(struct Animation) + name_bytes;
    if (sCurrentMod->animation_count >= MOD_MAX_CUSTOM_ANIMATIONS ||
        allocation_bytes > MOD_MAX_CUSTOM_ANIMATION_BYTES - sCurrentMod->animation_bytes)
        return luaL_error(L, "mod '%s' exceeded its custom animation budget", sCurrentMod->name);
    if (sAnimationCount >= MOD_MAX_CUSTOM_ANIMATION_GLOBAL_COUNT ||
        allocation_bytes > MOD_MAX_CUSTOM_ANIMATION_GLOBAL_BYTES - sAnimationBytes)
        return luaL_error(L, "global custom animation capacity is exhausted");

    s16 *values = malloc(values_length * sizeof(*values));
    u16 *index = malloc(index_length * sizeof(*index));
    if (!values || !index) {
        free(values);
        free(index);
        return luaL_error(L, "out of memory registering animation '%s'", name);
    }

    for (size_t i = 0; i < values_length; ++i) {
        lua_rawgeti(L, 7, (lua_Integer)i + 1);
        int is_integer = 0;
        lua_Integer value = lua_tointegerx(L, -1, &is_integer);
        lua_pop(L, 1);
        if (!is_integer || value < INT16_MIN || value > UINT16_MAX) {
            free(values);
            free(index);
            return luaL_error(L, "animation '%s' values[%zu] must be a 16-bit signed value or unsigned bit pattern", name, i + 1);
        }
        u16 bits = (u16)value;
        memcpy(&values[i], &bits, sizeof(bits));
    }
    for (size_t i = 0; i < index_length; ++i) {
        lua_rawgeti(L, 8, (lua_Integer)i + 1);
        int is_integer = 0;
        lua_Integer value = lua_tointegerx(L, -1, &is_integer);
        lua_pop(L, 1);
        if (!is_integer || value < 0 || value > UINT16_MAX || (size_t)value >= values_length) {
            free(values);
            free(index);
            return luaL_error(L, "animation '%s' index[%zu] must address an existing values entry", name, i + 1);
        }
        index[i] = (u16)value;
    }

    ModAnimation *registered = calloc(1, sizeof(*registered));
    struct Animation *animation = calloc(1, sizeof(*animation));
    char *owned_name = strdup(name);
    if (!registered || !animation || !owned_name) {
        free(values);
        free(index);
        free(registered);
        free(animation);
        free(owned_name);
        return luaL_error(L, "out of memory registering animation '%s'", name);
    }
    animation->flags = (s16)flags;
    animation->unk02 = (s16)y_divisor;
    animation->unk04 = (s16)start_frame;
    animation->unk06 = (s16)loop_start;
    animation->unk08 = (s16)loop_end;
    animation->unk0A = 0;
    animation->values = values;
    animation->index = index;
    animation->length = 0;

    registered->owner = sCurrentMod;
    registered->name = owned_name;
    registered->animation = animation;
    registered->values = values;
    registered->index = index;
    registered->values_length = values_length;
    registered->index_length = index_length;
    registered->bytes = allocation_bytes;
    registered->next_global = sAnimations;
    sAnimations = registered;
    registered->next_owned = sCurrentMod->animations;
    sCurrentMod->animations = registered;
    ++sCurrentMod->animation_count;
    sCurrentMod->animation_bytes += allocation_bytes;
    ++sAnimationCount;
    sAnimationBytes += allocation_bytes;
    mod_logf("info", sCurrentMod->name, "registered custom animation '%s' (%zu values, %zu indices)",
             name, values_length, index_length);
    return 0;
}

static int l_smlua_anim_util_set_animation(lua_State *L) {
    struct Object *object = lua_isnoneornil(L, 1) ? NULL : checked_object(L, 1);
    const char *name = luaL_checkstring(L, 2);
    if (!object) {
        if (sCurrentMod && !sCurrentMod->animation_warned) {
            sCurrentMod->animation_warned = true;
            mod_log("warning", sCurrentMod->name,
                    "smlua_anim_util_set_animation received a null object; no animation was changed");
        }
        return 0;
    }

    ModAnimation *animation = find_custom_animation(name);
    if (!animation) {
        if (sCurrentMod && !sCurrentMod->animation_warned) {
            sCurrentMod->animation_warned = true;
            mod_logf("warning", sCurrentMod->name,
                     "custom animation '%s' is not registered by an enabled mod; object animation unchanged", name);
        }
        return 0;
    }
    /* Match CoopDX: set curAnim directly; the game's existing animation
     * advancement and renderer consume the registered native Animation. */
    object->header.gfx.unk38.curAnim = animation->animation;
    return 0;
}

static int l_get_current_save_file_num(lua_State *L) {
    lua_pushinteger(L, gCurrSaveFileNum);
    return 1;
}

static int l_level_is_vanilla_level(lua_State *L) {
    lua_Integer level = luaL_checkinteger(L, 1);
    if (level < INT16_MIN || level > INT16_MAX)
        return luaL_argerror(L, 1, "level number must fit CoopDX's signed 16-bit parameter");
    bool vanilla = level >= LEVEL_MIN && level < LEVEL_COUNT && sVanillaLevelIds[level];
#ifdef IOS_HYRULE_WORLD
    if (level == LEVEL_HYRULE_FIELD) vanilla = false;
#endif
    lua_pushboolean(L, vanilla);
    return 1;
}

#define MOD_LEVEL_PARSE_MAX_COMMANDS 65536u
#define MOD_LEVEL_PARSE_MAX_VISITED 8192u
#define MOD_LEVEL_PARSE_MAX_DEPTH 32u
#define MOD_LEVEL_PARSE_MAX_MACRO_OBJECTS 8192u

typedef struct {
    Mod *owner;
    const char *file;
    int callback_ref;
    const void *visited[MOD_LEVEL_PARSE_MAX_VISITED];
    unsigned visited_count;
    unsigned command_count;
    unsigned callback_count;
    bool failed;
} ModLevelScriptParser;

static uintptr_t level_script_read_word(const void *command, size_t logical_offset) {
    uintptr_t value = 0;
    memcpy(&value, (const u8 *)command + CMD_PROCESS_OFFSET(logical_offset), sizeof(value));
    return value;
}

static uint32_t level_script_read_u32(const void *command, size_t logical_offset) {
    uint32_t value = 0;
    memcpy(&value, (const u8 *)command + CMD_PROCESS_OFFSET(logical_offset), sizeof(value));
    return value;
}

static int16_t level_script_read_s16(const void *command, size_t logical_offset) {
    int16_t value = 0;
    memcpy(&value, (const u8 *)command + CMD_PROCESS_OFFSET(logical_offset), sizeof(value));
    return value;
}

static bool level_script_mark_visited(ModLevelScriptParser *parser, const void *command) {
    for (unsigned i = 0; i < parser->visited_count; ++i)
        if (parser->visited[i] == command) return false;
    if (parser->visited_count >= MOD_LEVEL_PARSE_MAX_VISITED) {
        parser->failed = true;
        return false;
    }
    parser->visited[parser->visited_count++] = command;
    return true;
}

static lua_Integer level_script_behavior_id(const BehaviorScript *behavior) {
    ModBehavior *custom = find_behavior_by_script(behavior);
    return custom ? (lua_Integer)custom->id : native_behavior_id_from_script(behavior);
}

static bool level_script_parse_callback(ModLevelScriptParser *parser, int area_index,
                                       const void *command, const MacroObject *macro_objects) {
    lua_State *L = sLua;
    if (++parser->callback_count > MOD_LEVEL_PARSE_MAX_COMMANDS) {
        parser->failed = true;
        return false;
    }

    if (area_index >= 0) lua_pushinteger(L, area_index);
    else lua_pushnil(L);

    if (command && ((const u8 *)command)[0] == 0x24) {
        const BehaviorScript *behavior = (const BehaviorScript *)level_script_read_word(command, 20);
        behavior = segmented_to_virtual(behavior);
        lua_newtable(L);
        lua_pushinteger(L, level_script_behavior_id(behavior)); lua_setfield(L, -2, "behavior");
        lua_pushinteger(L, level_script_read_u32(command, 16)); lua_setfield(L, -2, "behaviorArg");
        lua_pushinteger(L, ((const u8 *)command)[3]); lua_setfield(L, -2, "model");
        lua_pushinteger(L, level_script_read_s16(command, 4)); lua_setfield(L, -2, "posX");
        lua_pushinteger(L, level_script_read_s16(command, 6)); lua_setfield(L, -2, "posY");
        lua_pushinteger(L, level_script_read_s16(command, 8)); lua_setfield(L, -2, "posZ");
        lua_pushinteger(L, level_script_read_s16(command, 10) * 0x8000 / 180); lua_setfield(L, -2, "pitch");
        lua_pushinteger(L, level_script_read_s16(command, 12) * 0x8000 / 180); lua_setfield(L, -2, "yaw");
        lua_pushinteger(L, level_script_read_s16(command, 14) * 0x8000 / 180); lua_setfield(L, -2, "roll");
    } else lua_pushnil(L);

    if (macro_objects) {
        lua_newtable(L);
        int behavior_table = lua_gettop(L);
        lua_newtable(L);
        int args_table = lua_gettop(L);
        lua_newtable(L);
        int model_table = lua_gettop(L);
        for (unsigned i = 0; i < MOD_LEVEL_PARSE_MAX_MACRO_OBJECTS; ++i) {
            int16_t packed = macro_objects[i * 5];
            if (packed == -1) break;
            int preset_id = (packed & 0x1FF) - 0x1F;
            if (preset_id < 0 || (unsigned)preset_id >= gMacroObjectPresetCount) break;
            const struct MacroPreset *preset = &MacroObjectPresets[preset_id];
            int obj_params = (macro_objects[i * 5 + 4] & 0xFF00) | (preset->param & 0x00FF);
            int behavior_params = ((obj_params & 0x00FF) << 16) | (obj_params & 0xFF00);
            lua_pushinteger(L, i); lua_pushinteger(L, level_script_behavior_id(preset->behavior));
            lua_rawset(L, behavior_table);
            lua_pushinteger(L, i); lua_pushinteger(L, behavior_params);
            lua_rawset(L, args_table);
            lua_pushinteger(L, i); lua_pushinteger(L, preset->model);
            lua_rawset(L, model_table);
        }
    } else {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushnil(L);
    }

    if (!call_ref(parser->owner, parser->file, parser->callback_ref,
                  "level_script_parse callback", 5, 0)) return false;
    return true;
}

static bool level_script_walk(ModLevelScriptParser *parser, const LevelScript *script, unsigned depth) {
    if (!script) return true;
    if (depth >= MOD_LEVEL_PARSE_MAX_DEPTH) {
        parser->failed = true;
        return false;
    }
    const u8 *command = segmented_to_virtual(script);
    while (command) {
        if (++parser->command_count > MOD_LEVEL_PARSE_MAX_COMMANDS) {
            parser->failed = true;
            return false;
        }
        if (!level_script_mark_visited(parser, command)) return !parser->failed;

        u8 type = command[0];
        u8 size = command[1];
        size_t command_bytes = (size_t)size << (sizeof(void *) >> 3);
        if (size < 4 || command_bytes < sizeof(uintptr_t) || command_bytes > 4096) {
            parser->failed = true;
            return false;
        }
        const u8 *next = command + command_bytes;

        if (type == 0x1F) {
            if (!level_script_parse_callback(parser, command[2], NULL, NULL)) return !parser->failed;
        } else if (type == 0x24) {
            if (!level_script_parse_callback(parser, -1, command, NULL)) return !parser->failed;
        } else if (type == 0x39) {
            const MacroObject *objects = (const MacroObject *)level_script_read_word(command, 4);
            objects = segmented_to_virtual(objects);
            if (objects && !level_script_parse_callback(parser, -1, NULL, objects)) return !parser->failed;
        }

        switch (type) {
            case 0x00: /* EXECUTE */
            case 0x06: /* JUMP_LINK */
            case 0x0C: /* JUMP_IF: parse both possible paths */
            case 0x0D: { /* JUMP_LINK_IF */
                size_t target_offset = type == 0x00 ? 12 : (type == 0x06 ? 4 : 8);
                const LevelScript *target = (const LevelScript *)level_script_read_word(command, target_offset);
                if (target && !level_script_walk(parser, target, depth + 1)) return false;
                if (type == 0x00) return true; /* EXECUTE replaces its current script frame. */
                command = next;
                break;
            }
            case 0x01: { /* EXIT_AND_EXECUTE */
                const LevelScript *target = (const LevelScript *)level_script_read_word(command, 12);
                return target ? level_script_walk(parser, target, depth + 1) : true;
            }
            case 0x02: /* EXIT */
            case 0x07: /* RETURN */
                return true;
            case 0x05: { /* JUMP */
                const LevelScript *target = (const LevelScript *)level_script_read_word(command, 4);
                if (!target) return true;
                command = segmented_to_virtual(target);
                break;
            }
            case 0x42: { /* CoopDX JUMP_AREA */
                const LevelScript *target = (const LevelScript *)level_script_read_word(command, 8);
                if (target && !level_script_walk(parser, target, depth + 1)) return false;
                command = next;
                break;
            }
            default:
                command = next;
                break;
        }
    }
    return true;
}

static int l_level_script_parse(lua_State *L) {
    lua_Integer level = luaL_checkinteger(L, 1);
    if (level < INT16_MIN || level > INT16_MAX)
        return luaL_argerror(L, 1, "level number must fit CoopDX's signed 16-bit parameter");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!sCurrentMod) return luaL_error(L, "level_script_parse must be called while loading a mod or from one of its callbacks");
    const LevelScript *script = level_script_for_mod_api((s16)level);
    if (!script) return luaL_error(L, "level_script_parse has no built-in script for level %d; DynOS/custom level parsing is unavailable", (int)level);

    lua_pushvalue(L, 2);
    int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ModLevelScriptParser parser = { .owner = sCurrentMod, .file = sCurrentFile, .callback_ref = callback_ref };
    bool walked = level_script_walk(&parser, script, 0);
    luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
    if (!walked || parser.failed)
        return luaL_error(L, "level_script_parse exceeded its bounded native-script traversal limits for level %d", (int)level);
    return 0;
}

static int l_save_file_get_flags(lua_State *L) {
    lua_pushinteger(L, save_file_get_flags());
    return 1;
}

static int l_save_file_get_star_flags(lua_State *L) {
    int file_index = (int)luaL_checkinteger(L, 1);
    int course_index = (int)luaL_checkinteger(L, 2);
    if (file_index < 0 || file_index >= NUM_SAVE_FILES ||
        course_index < -1 || course_index >= COURSE_COUNT)
        return luaL_error(L, "save_file_get_star_flags() index is outside this engine's save table");
    lua_pushinteger(L, save_file_get_star_flags(file_index, course_index));
    return 1;
}

#define MOD_MAX_ACTS 6
#define MOD_STAR_NAME_CAPACITY 256

static const char *sm64_char_to_ascii(u8 c) {
    static const char *const special[256] = {
        [0x3E] = "'", [0x3F] = ".", [0x6F] = ",", [0x9E] = " ", [0x9F] = "-",
        [0xD0] = "/", [0xD1] = "the", [0xD2] = "you", [0xE0] = "[%]",
        [0xE1] = "(", [0xE2] = ")(", [0xE3] = ")", [0xE4] = "+", [0xE5] = "&",
        [0xE6] = ":", [0xF2] = "!", [0xF3] = "%", [0xF4] = "?", [0xF5] = "<<",
        [0xF6] = ">>", [0xF7] = "~", [0xF9] = "$", [0xFA] = "★", [0xFB] = "*",
        [0xFC] = "•", [0xFD] = "=", [0xFE] = "\n",
#ifdef VERSION_EU
        [0x60] = "à", [0x61] = "â", [0x62] = "ä", [0x64] = "À", [0x65] = "Â", [0x66] = "Ä",
        [0x70] = "è", [0x71] = "ê", [0x72] = "ë", [0x73] = "é", [0x74] = "È", [0x75] = "Ê",
        [0x76] = "Ë", [0x77] = "É", [0x80] = "ù", [0x81] = "û", [0x82] = "ü", [0x84] = "Ù",
        [0x85] = "Û", [0x86] = "Ü", [0x91] = "ô", [0x92] = "ö", [0x95] = "Ô", [0x96] = "Ö",
        [0xA1] = "î", [0xA2] = "ï", [0xEC] = "ß", [0xED] = "Ç", [0xEE] = "ç", [0xF0] = "„",
#endif
    };
    if (c <= 0x09) {
        static const char *const digit[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
        return digit[c];
    }
    if (c >= 0x0A && c <= 0x23) {
        static const char *const upper[] = {
            "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
            "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
        };
        return upper[c - 0x0A];
    }
    if (c >= 0x24 && c <= 0x3D) {
        static const char *const lower[] = {
            "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
            "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"
        };
        return lower[c - 0x24];
    }
    return special[c] ? special[c] : " ";
}

static void mod_decode_sm64_text(char output[MOD_STAR_NAME_CAPACITY], const u8 *encoded) {
    size_t written = 0;
    if (encoded) {
        for (size_t i = 0; i < 64 && encoded[i] != 0xFF; ++i) {
            const char *glyph = sm64_char_to_ascii(encoded[i]);
            size_t glyph_length = strlen(glyph);
            if (glyph_length > MOD_STAR_NAME_CAPACITY - written - 1) break;
            memcpy(output + written, glyph, glyph_length);
            written += glyph_length;
        }
    }
    output[written] = '\0';
}

static void mod_capitalize_ascii(char *text) {
    for (; *text; ++text)
        if (*text >= 'a' && *text <= 'z') *text = (char)(*text + ('A' - 'a'));
}

static void mod_decapitalize_ascii(char *text) {
    bool decapitalize = false;
    for (; *text; ++text) {
        if (*text >= 'A' && *text <= 'Z') {
            if (decapitalize) *text = (char)(*text + ('a' - 'A'));
            else decapitalize = true;
        } else if (*text < '0' && *text != '\'') {
            decapitalize = false;
        }
    }
}

static int l_get_star_name_ascii(lua_State *L) {
    lua_Integer course_arg = luaL_checkinteger(L, 1);
    lua_Integer star_arg = luaL_checkinteger(L, 2);
    lua_Integer case_arg = luaL_checkinteger(L, 3);
    if (course_arg < INT16_MIN || course_arg > INT16_MAX)
        return luaL_argerror(L, 1, "course number must fit CoopDX's signed 16-bit parameter");
    if (star_arg < INT16_MIN || star_arg > INT16_MAX)
        return luaL_argerror(L, 2, "star number must fit CoopDX's signed 16-bit parameter");
    if (case_arg < INT16_MIN || case_arg > INT16_MAX)
        return luaL_argerror(L, 3, "character-case mode must fit CoopDX's signed 16-bit parameter");

    static char output[MOD_STAR_NAME_CAPACITY];
    s16 course = (s16)course_arg;
    s16 star = (s16)star_arg;
    s16 char_case = (s16)case_arg;
    s16 star_index = star - 1;

    if (COURSE_IS_MAIN_COURSE(course)) {
        if (star_index >= 0 && star_index < MOD_MAX_ACTS) {
            void **act_name_table = segmented_to_virtual(seg2_act_name_table);
#ifdef VERSION_EU
            switch (gInGameLanguage) {
                case LANGUAGE_FRENCH: act_name_table = segmented_to_virtual(act_name_table_eu_fr); break;
                case LANGUAGE_GERMAN: act_name_table = segmented_to_virtual(act_name_table_eu_de); break;
                case LANGUAGE_ENGLISH: act_name_table = segmented_to_virtual(act_name_table_eu_en); break;
            }
#endif
            size_t table_index = (size_t)(course - COURSE_BOB) * MOD_MAX_ACTS + (size_t)star_index;
            const u8 *encoded = segmented_to_virtual(act_name_table[table_index]);
            if (!encoded) return luaL_error(L, "get_star_name_ascii has no native act name for course %d, star %d", course, star);
            mod_decode_sm64_text(output, encoded);
            /* Upstream preserves vanilla act-name capitalization when charCase is zero. */
            if (char_case == -1) mod_decapitalize_ascii(output);
            else if (char_case == 1) mod_capitalize_ascii(output);
            lua_pushstring(L, output);
            return 1;
        }
        if (star == 7) {
            snprintf(output, sizeof(output), "%d Coins Star",
                     mod_runtime_level_value(MOD_LEVEL_VALUE_COINS_REQUIRED_FOR_COIN_STAR));
        } else {
            snprintf(output, sizeof(output), "A Secret Star!");
        }
    } else if (course == COURSE_NONE) {
        switch (star) {
            case 1: snprintf(output, sizeof(output), "Toad Star 1"); break;
            case 2: snprintf(output, sizeof(output), "Toad Star 2"); break;
            case 3: snprintf(output, sizeof(output), "Toad Star 3"); break;
            case 4: snprintf(output, sizeof(output), "Mips Star 1"); break;
            case 5: snprintf(output, sizeof(output), "Mips Star 2"); break;
            default: snprintf(output, sizeof(output), "A Secret Star!"); break;
        }
    } else if (course <= COURSE_MAX) {
        snprintf(output, sizeof(output), "Star %d", star);
    } else {
        snprintf(output, sizeof(output), "A Secret Star!");
    }

    if (char_case == -1) mod_decapitalize_ascii(output);
    else if (char_case == 1) mod_capitalize_ascii(output);
    lua_pushstring(L, output);
    return 1;
}

static int l_get_level_name_ascii(lua_State *L) {
    lua_Integer course_arg = luaL_checkinteger(L, 1);
    lua_Integer level_arg = luaL_checkinteger(L, 2);
    lua_Integer area_arg = luaL_checkinteger(L, 3);
    lua_Integer case_arg = luaL_checkinteger(L, 4);
    if (course_arg < INT16_MIN || course_arg > INT16_MAX)
        return luaL_argerror(L, 1, "course number must fit CoopDX's signed 16-bit parameter");
    if (level_arg < INT16_MIN || level_arg > INT16_MAX)
        return luaL_argerror(L, 2, "level number must fit CoopDX's signed 16-bit parameter");
    if (area_arg < INT16_MIN || area_arg > INT16_MAX)
        return luaL_argerror(L, 3, "area index must fit CoopDX's signed 16-bit parameter");
    if (case_arg < INT16_MIN || case_arg > INT16_MAX)
        return luaL_argerror(L, 4, "character-case mode must fit CoopDX's signed 16-bit parameter");

    s16 course = (s16)course_arg;
    s16 level = (s16)level_arg;
    s16 area = (s16)area_arg;
    s16 char_case = (s16)case_arg;
    static char output[MOD_STAR_NAME_CAPACITY];

    /* CoopDX starts vanilla course-name strings after the three menu metadata bytes. */
    if (course >= COURSE_MIN && course < COURSE_MAX) {
        void **course_name_table = segmented_to_virtual(seg2_course_name_table);
#ifdef VERSION_EU
        switch (gInGameLanguage) {
            case LANGUAGE_FRENCH: course_name_table = segmented_to_virtual(course_name_table_eu_fr); break;
            case LANGUAGE_GERMAN: course_name_table = segmented_to_virtual(course_name_table_eu_de); break;
            case LANGUAGE_ENGLISH: course_name_table = segmented_to_virtual(course_name_table_eu_en); break;
        }
#endif
        size_t table_index = (size_t)(course - COURSE_BOB);
        const u8 *encoded = course_name_table
            ? segmented_to_virtual(course_name_table[table_index])
            : NULL;
        if (!encoded)
            return luaL_error(L, "get_level_name_ascii has no native course name for course %d", course);
        mod_decode_sm64_text(output, encoded + 3);
        /* The native table already carries the vanilla title casing. */
        if (char_case > 0) char_case = 0;
    } else if (course == COURSE_NONE) {
        switch (level) {
            case LEVEL_CASTLE:
                switch (area) {
                    case 1: snprintf(output, sizeof(output), "Castle Main Floor"); break;
                    case 2: snprintf(output, sizeof(output), "Castle Upper Floor"); break;
                    case 3: snprintf(output, sizeof(output), "Castle Basement"); break;
                    default: snprintf(output, sizeof(output), "Castle Purgatory"); break;
                }
                break;
            case LEVEL_CASTLE_GROUNDS: snprintf(output, sizeof(output), "Castle Grounds"); break;
            case LEVEL_CASTLE_COURTYARD: snprintf(output, sizeof(output), "Castle Courtyard"); break;
            default: snprintf(output, sizeof(output), "Peach's Castle"); break;
        }
    } else if (course == LEVEL_ENDING) {
        snprintf(output, sizeof(output), "Ending");
    } else {
        snprintf(output, sizeof(output), "Peach's Castle");
    }

    if (char_case == -1) mod_decapitalize_ascii(output);
    else if (char_case == 1) mod_capitalize_ascii(output);
    lua_pushstring(L, output);
    return 1;
}

static void mod_storage_free_entries(Mod *mod) {
    while (mod && mod->storage) {
        ModStorageEntry *next = mod->storage->next;
        free(mod->storage->key);
        free(mod->storage->value);
        free(mod->storage);
        mod->storage = next;
    }
    if (mod) {
        mod->storage_count = 0;
        mod->storage_bytes = 0;
    }
}

static bool mod_storage_paths(const Mod *mod, char *relative, size_t relative_size,
                              char *absolute, size_t absolute_size) {
    static const char hex[] = "0123456789abcdef";
    size_t n = strlen(mod->name);
    if (n * 2 + sizeof("mods_storage/.sav") > relative_size) return false;
    int prefix = snprintf(relative, relative_size, "mods_storage/");
    if (prefix < 0 || (size_t)prefix + n * 2 + sizeof(".sav") > relative_size) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)mod->name[i];
        relative[prefix + i * 2] = hex[c >> 4];
        relative[prefix + i * 2 + 1] = hex[c & 15];
    }
    memcpy(relative + prefix + n * 2, ".sav", sizeof(".sav"));
    const char *write_path = fs_get_write_path(relative);
    if (!write_path) return false;
    int written = snprintf(absolute, absolute_size, "%s", write_path);
    if (written < 0 || (size_t)written >= absolute_size) return false;
    return true;
}

static int mod_storage_hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *mod_storage_decode(const char *text, size_t length, size_t *out_length) {
    if ((length & 1u) || length / 2 > MOD_STORAGE_MAX_ENTRY) return NULL;
    size_t bytes = length / 2;
    char *decoded = malloc(bytes + 1);
    if (!decoded) return NULL;
    for (size_t i = 0; i < bytes; ++i) {
        int high = mod_storage_hex_value((unsigned char)text[i * 2]);
        int low = mod_storage_hex_value((unsigned char)text[i * 2 + 1]);
        if (high < 0 || low < 0 || (high == 0 && low == 0)) {
            free(decoded);
            return NULL;
        }
        decoded[i] = (char)((high << 4) | low);
    }
    decoded[bytes] = '\0';
    *out_length = bytes;
    return decoded;
}

static bool mod_storage_read_file(Mod *mod) {
    char relative[MOD_MAX_NAME * 2 + 32], path[SYS_MAX_PATH];
    if (!mod_storage_paths(mod, relative, sizeof(relative), path, sizeof(path))) return false;
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) { mod->storage_loaded = true; return true; }
        return false;
    }
    char line[MOD_STORAGE_MAX_ENTRY * 4 + 4];
    unsigned lines = 0;
    bool valid = fgets(line, sizeof(line), file) != NULL && strcmp(line, "SM64MODSTORAGE1\n") == 0;
    while (valid && fgets(line, sizeof(line), file)) {
        size_t line_length = strlen(line);
        if (!line_length || line[line_length - 1] != '\n') { valid = false; break; }
        line[--line_length] = '\0';
        if (line_length && line[line_length - 1] == '\r') line[--line_length] = '\0';
        char *equals = strchr(line, '=');
        if (!equals || ++lines > MOD_STORAGE_MAX_KEYS) { valid = false; break; }
        *equals = '\0';
        size_t key_length = 0, value_length = 0;
        char *key = mod_storage_decode(line, strlen(line), &key_length);
        char *value = mod_storage_decode(equals + 1, strlen(equals + 1), &value_length);
        if (!key || !value || !key_length || key_length > MOD_STORAGE_MAX_ENTRY ||
            value_length > MOD_STORAGE_MAX_ENTRY ||
            mod->storage_bytes + key_length + value_length > MOD_STORAGE_MAX_BYTES) {
            free(key); free(value); valid = false; break;
        }
        for (ModStorageEntry *at = mod->storage; at; at = at->next) {
            if (at->key_length == key_length && !memcmp(at->key, key, key_length)) {
                valid = false;
                break;
            }
        }
        if (!valid) { free(key); free(value); break; }
        ModStorageEntry *entry = calloc(1, sizeof(*entry));
        if (!entry) { free(key); free(value); valid = false; break; }
        entry->key = key; entry->key_length = key_length;
        entry->value = value; entry->value_length = value_length;
        entry->next = mod->storage; mod->storage = entry;
        ++mod->storage_count; mod->storage_bytes += key_length + value_length;
    }
    if (ferror(file)) valid = false;
    fclose(file);
    if (!valid) {
        mod_storage_free_entries(mod);
        mod->storage_failed = true;
        mod_log("error", mod->name, "mod storage is corrupt or exceeds the 1 MiB sandbox limit");
        return false;
    }
    mod->storage_loaded = true;
    return true;
}

static void mod_storage_write_hex(FILE *file, const char *data, size_t length) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)data[i];
        fputc(hex[c >> 4], file);
        fputc(hex[c & 15], file);
    }
}

static bool mod_storage_write_file(Mod *mod) {
    char relative[MOD_MAX_NAME * 2 + 32], path[SYS_MAX_PATH], temporary[SYS_MAX_PATH];
    char directory[SYS_MAX_PATH];
    int n = snprintf(directory, sizeof(directory), "%s/mods_storage", fs_writepath);
    if (n <= 0 || (size_t)n >= sizeof(directory)) return false;
    if (!fs_sys_dir_exists(directory) && !fs_sys_mkdir(directory)) return false;
    if (!mod_storage_paths(mod, relative, sizeof(relative), path, sizeof(path))) return false;
    n = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(temporary)) return false;
    FILE *file = fopen(temporary, "wb");
    if (!file) return false;
    bool valid = fputs("SM64MODSTORAGE1\n", file) >= 0;
    for (ModStorageEntry *entry = mod->storage; valid && entry; entry = entry->next) {
        mod_storage_write_hex(file, entry->key, entry->key_length);
        if (fputc('=', file) == EOF) { valid = false; break; }
        mod_storage_write_hex(file, entry->value, entry->value_length);
        if (fputc('\n', file) == EOF) valid = false;
    }
    if (fflush(file) != 0 || ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;
    if (valid && rename(temporary, path) == 0) return true;
    unlink(temporary);
    return false;
}

static ModStorageEntry *mod_storage_find(Mod *mod, const char *key, size_t key_length) {
    for (ModStorageEntry *entry = mod->storage; entry; entry = entry->next)
        if (entry->key_length == key_length && !memcmp(entry->key, key, key_length)) return entry;
    return NULL;
}

static int l_mod_storage_load(lua_State *L) {
    Mod *mod = sCurrentMod;
    if (!mod) return luaL_error(L, "mod_storage_load() is only available while a mod is executing");
    size_t key_length = 0, default_length = 0;
    const char *key = luaL_checklstring(L, 1, &key_length);
    const char *default_value = luaL_optlstring(L, 2, NULL, &default_length);
    if (!key_length || key_length > MOD_STORAGE_MAX_ENTRY || memchr(key, '\0', key_length))
        return luaL_error(L, "mod_storage_load() key must contain 1..%d bytes", MOD_STORAGE_MAX_ENTRY);
    if (!mod->storage_loaded && !mod_storage_read_file(mod)) {
        if (!mod->storage_failed) mod_log("error", mod->name, "could not read sandboxed mod storage");
    }
    ModStorageEntry *entry = mod_storage_find(mod, key, key_length);
    if (entry) lua_pushlstring(L, entry->value, entry->value_length);
    else if (default_value) lua_pushlstring(L, default_value, default_length);
    else lua_pushnil(L);
    return 1;
}

static int l_mod_storage_save(lua_State *L) {
    Mod *mod = sCurrentMod;
    if (!mod) return luaL_error(L, "mod_storage_save() is only available while a mod is executing");
    size_t key_length = 0, value_length = 0;
    const char *key = luaL_checklstring(L, 1, &key_length);
    const char *value = luaL_checklstring(L, 2, &value_length);
    if (!key_length || key_length > MOD_STORAGE_MAX_ENTRY || value_length > MOD_STORAGE_MAX_ENTRY ||
        memchr(key, '\0', key_length) || memchr(value, '\0', value_length))
        return luaL_error(L, "mod_storage_save() keys and values are limited to 1024 non-NUL bytes");
    if (!mod->storage_loaded && !mod_storage_read_file(mod)) {
        if (!mod->storage_failed) mod_log("error", mod->name, "could not read sandboxed mod storage");
        lua_pushboolean(L, false);
        return 1;
    }
    if (mod->storage_failed) { lua_pushboolean(L, false); return 1; }
    ModStorageEntry *entry = mod_storage_find(mod, key, key_length);
    size_t old_value_length = entry ? entry->value_length : 0;
    size_t old_total = entry ? entry->key_length + old_value_length : 0;
    size_t new_total = key_length + value_length;
    if ((!entry && mod->storage_count >= MOD_STORAGE_MAX_KEYS) ||
        mod->storage_bytes - old_total + new_total > MOD_STORAGE_MAX_BYTES) {
        lua_pushboolean(L, false);
        return 1;
    }
    char *new_value = malloc(value_length + 1);
    if (!new_value) { lua_pushboolean(L, false); return 1; }
    memcpy(new_value, value, value_length); new_value[value_length] = '\0';
    bool created = false;
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) { free(new_value); lua_pushboolean(L, false); return 1; }
        entry->key = malloc(key_length + 1);
        if (!entry->key) { free(new_value); free(entry); lua_pushboolean(L, false); return 1; }
        memcpy(entry->key, key, key_length); entry->key[key_length] = '\0';
        entry->key_length = key_length;
        entry->next = mod->storage; mod->storage = entry;
        ++mod->storage_count; created = true;
    }
    char *old_value = entry->value;
    entry->value = new_value; entry->value_length = value_length;
    mod->storage_bytes = mod->storage_bytes - old_total + new_total;
    if (!mod_storage_write_file(mod)) {
        mod->storage_bytes = mod->storage_bytes - new_total + old_total;
        if (created) {
            mod->storage = entry->next;
            --mod->storage_count;
            free(entry->key); free(entry);
        } else {
            entry->value = old_value; entry->value_length = old_value_length;
        }
        free(new_value);
        lua_pushboolean(L, false);
        return 1;
    }
    free(old_value);
    lua_pushboolean(L, true);
    return 1;
}

static int mod_menu_add(lua_State *L, Mod *mod, int label_index, int callback_index,
                        int type, lua_Integer value, lua_Integer minimum, lua_Integer maximum) {
    size_t label_length = 0;
    const char *label = luaL_checklstring(L, label_index, &label_length);
    luaL_checktype(L, callback_index, LUA_TFUNCTION);
    if (!label_length || label_length >= sizeof(((ModMenuItem *)0)->label) ||
        memchr(label, '\0', label_length))
        return luaL_error(L, "mod menu labels must contain 1..255 non-NUL bytes");
    if (type == MOD_MENU_SLIDER && (minimum > maximum || value < minimum || value > maximum))
        return luaL_error(L, "mod menu slider default must be inside its minimum/maximum range");
    char *file = strdup(sCurrentFile ? sCurrentFile : "main.lua");
    if (!file) return luaL_error(L, "out of memory storing mod menu source");
    lua_pushvalue(L, callback_index);
    int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    pthread_mutex_lock(&sModListMutex);
    if (mod->menu_item_count >= 256) {
        pthread_mutex_unlock(&sModListMutex);
        free(file); luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
        return luaL_error(L, "mod '%s' exceeded the 256 item menu limit", mod->name);
    }
    if (mod->menu_item_count == mod->menu_item_capacity) {
        size_t capacity = mod->menu_item_capacity ? mod->menu_item_capacity * 2 : 8;
        ModMenuItem *items = realloc(mod->menu_items, capacity * sizeof(*items));
        if (!items) {
            pthread_mutex_unlock(&sModListMutex);
            free(file); luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
            return luaL_error(L, "out of memory registering mod menu item");
        }
        mod->menu_items = items; mod->menu_item_capacity = capacity;
    }
    ModMenuItem *item = &mod->menu_items[mod->menu_item_count];
    memset(item, 0, sizeof(*item));
    memcpy(item->label, label, label_length); item->label[label_length] = '\0';
    item->type = type; item->value = value; item->minimum = minimum; item->maximum = maximum;
    item->file = file;
    item->callback_ref = callback_ref;
    lua_Integer item_index = (lua_Integer)mod->menu_item_count;
    ++mod->menu_item_count;
    pthread_mutex_unlock(&sModListMutex);
    lua_pushinteger(L, item_index);
    return 1;
}

static int l_hook_mod_menu_button(lua_State *L) {
    if (!sCurrentMod) return luaL_error(L, "hook_mod_menu_button() must be called while loading a mod");
    return mod_menu_add(L, sCurrentMod, 1, 2, MOD_MENU_BUTTON, 0, 0, 0);
}

static int l_hook_mod_menu_checkbox(lua_State *L) {
    if (!sCurrentMod) return luaL_error(L, "hook_mod_menu_checkbox() must be called while loading a mod");
    return mod_menu_add(L, sCurrentMod, 1, 3, MOD_MENU_CHECKBOX,
                        lua_toboolean(L, 2) ? 1 : 0, 0, 1);
}

static int l_hook_mod_menu_slider(lua_State *L) {
    if (!sCurrentMod) return luaL_error(L, "hook_mod_menu_slider() must be called while loading a mod");
    return mod_menu_add(L, sCurrentMod, 1, 5, MOD_MENU_SLIDER,
                        luaL_checkinteger(L, 2), luaL_checkinteger(L, 3), luaL_checkinteger(L, 4));
}

static int l_hook_chat_command(lua_State *L) {
    Mod *mod = sCurrentMod;
    if (!mod) return luaL_error(L, "hook_chat_command() must be called while loading a mod");
    size_t name_length = 0, description_length = 0;
    const char *name = luaL_checklstring(L, 1, &name_length);
    const char *description = luaL_checklstring(L, 2, &description_length);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    if (!name_length || name_length >= sizeof(((ModCommand *)0)->name) ||
        description_length >= sizeof(((ModCommand *)0)->description) ||
        memchr(name, '\0', name_length) || memchr(description, '\0', description_length))
        return luaL_error(L, "chat command names must be 1..63 bytes and descriptions at most 255 bytes");
    for (size_t i = 0; i < name_length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 128 || !(isalnum(c) || c == '-' || c == '_'))
            return luaL_error(L, "chat command names may contain only ASCII letters, digits, '-' and '_'");
    }
    unsigned count = 0;
    for (ModCommand *at = mod->commands; at; at = at->next) {
        ++count;
        if (!strcasecmp(at->name, name)) return luaL_error(L, "mod '%s' registered duplicate chat command '%s'", mod->name, name);
    }
    if (count >= MOD_MAX_COMMANDS) return luaL_error(L, "mod '%s' exceeded the chat command limit", mod->name);
    ModCommand *command = calloc(1, sizeof(*command));
    if (!command) return luaL_error(L, "out of memory registering chat command");
    memcpy(command->name, name, name_length); command->name[name_length] = '\0';
    memcpy(command->description, description, description_length); command->description[description_length] = '\0';
    command->file = strdup(sCurrentFile ? sCurrentFile : "main.lua");
    if (!command->file) { free(command); return luaL_error(L, "out of memory storing chat command source"); }
    lua_pushvalue(L, 3); command->callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    command->next = mod->commands; mod->commands = command;
    return 0;
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
                 || !strncmp(name, "interact", 8)
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

static int sync_table_index(lua_State *L) {
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(1));
    return 1;
}

static int sync_table_pairs(lua_State *L) {
    lua_getglobal(L, "next");
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushnil(L);
    return 3;
}

static int sync_table_length(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)lua_rawlen(L, lua_upvalueindex(1)));
    return 1;
}

static void push_sync_hook_ref(lua_State *L, int ref) {
    if (ref == LUA_REFNIL || ref == LUA_NOREF) lua_pushnil(L);
    else lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
}

static int sync_table_newindex(lua_State *L) {
    Mod *owner = (Mod *)lua_touserdata(L, lua_upvalueindex(2));
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(1));
    int old_value_index = lua_gettop(L);
    bool changed = !lua_rawequal(L, 3, old_value_index);

    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, lua_upvalueindex(1));
    if (!changed || !owner || !sLua) {
        lua_pop(L, 1);
        return 0;
    }

    /* Keep the old value on the Lua stack while each callback gets tag,
     * oldValue and newValue. Nested changes run through this same path. */
    for (ModSyncHook *hook = sSyncHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || hook->owner != owner) continue;
        push_sync_hook_ref(L, hook->table_ref);
        bool same_table = lua_rawequal(L, -1, 1);
        lua_pop(L, 1);
        if (!same_table) continue;
        push_sync_hook_ref(L, hook->field_ref);
        bool same_field = lua_rawequal(L, -1, 2);
        lua_pop(L, 1);
        if (!same_field) continue;

        push_sync_hook_ref(L, hook->tag_ref);
        lua_pushvalue(L, old_value_index);
        lua_pushvalue(L, 3);
        if (!call_ref(hook->owner, hook->file, hook->function_ref,
                      "hook_on_sync_table_change", 3, 0)) {
            hook->enabled = false;
            unref_if_valid(hook->table_ref); hook->table_ref = LUA_NOREF;
            unref_if_valid(hook->field_ref); hook->field_ref = LUA_NOREF;
            unref_if_valid(hook->tag_ref); hook->tag_ref = LUA_NOREF;
            unref_if_valid(hook->function_ref); hook->function_ref = LUA_NOREF;
        }
    }
    lua_pop(L, 1);
    return 0;
}

static void sync_table_new(lua_State *L, Mod *mod) {
    lua_newtable(L); /* proxy */
    int proxy_index = lua_absindex(L, -1);
    lua_newtable(L); /* private backing table */
    int backing_index = lua_absindex(L, -1);
    lua_newtable(L); /* protected metatable */

    lua_pushvalue(L, backing_index);
    lua_pushcclosure(L, sync_table_index, 1);
    lua_setfield(L, -2, "__index");
    lua_pushvalue(L, backing_index);
    lua_pushlightuserdata(L, mod);
    lua_pushcclosure(L, sync_table_newindex, 2);
    lua_setfield(L, -2, "__newindex");
    lua_pushvalue(L, backing_index);
    lua_pushcclosure(L, sync_table_pairs, 1);
    lua_setfield(L, -2, "__pairs");
    lua_pushvalue(L, backing_index);
    lua_pushcclosure(L, sync_table_length, 1);
    lua_setfield(L, -2, "__len");
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "__sm64_sync_table");
    lua_pushliteral(L, "CoopDX SyncTable");
    lua_setfield(L, -2, "__metatable");

    lua_setmetatable(L, proxy_index);
    lua_remove(L, backing_index);
}

static bool set_mod_environment(lua_State *L, int function_index, Mod *mod) {
    function_index = lua_absindex(L, function_index);
    if (mod->env_ref == LUA_NOREF) {
        lua_newtable(L);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "_G");
        /* CoopDX exposes each loaded DynOS behavior symbol in the owning
         * mod's globals. Keep those IDs private to this mod environment. */
        for (ModBehavior *behavior = mod->behaviors; behavior; behavior = behavior->next_owned) {
            if (!behavior->dynos_behavior || !behavior->name) continue;
            lua_pushinteger(L, behavior->id);
            lua_setfield(L, -2, behavior->name);
        }
        sync_table_new(L, mod); lua_setfield(L, -2, "gGlobalSyncTable");
        lua_newtable(L);
        for (int player = 0; player < 16; ++player) {
            sync_table_new(L, mod);
            lua_rawseti(L, -2, player);
        }
        lua_setfield(L, -2, "gPlayerSyncTable");
        lua_newtable(L);
        int active_mods_index = 1;
        for (Mod *active = sMods; active; active = active->next) {
            if (!active->enabled) continue;
            lua_newtable(L);
            lua_pushstring(L, active->display_name[0] ? active->display_name : active->name); lua_setfield(L, -2, "name");
            lua_pushstring(L, active->name); lua_setfield(L, -2, "relativePath");
            lua_pushstring(L, active->incompatible); lua_setfield(L, -2, "incompatible");
            lua_pushstring(L, active->category); lua_setfield(L, -2, "category");
            lua_pushstring(L, active->description); lua_setfield(L, -2, "description");
            lua_rawseti(L, -2, active_mods_index++);
        }
        lua_setfield(L, -2, "gActiveMods");
        lua_newtable(L);
        lua_pushglobaltable(L); lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        mod->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (!mod->sync_tables_warned) {
            mod->sync_tables_warned = true;
            mod_log("warning", mod->name,
                    "gGlobalSyncTable and gPlayerSyncTable are isolated mod-local tables; values do not synchronize in single-player mode");
        }
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

/* CoopDX archives can ship alternate PUC Lua 5.3 chunks in the same mod,
 * commonly with -32.lua and -64.lua suffixes. Upstream validates each chunk
 * against the current process and skips a chunk built for a different size_t
 * width while continuing to load the rest of that mod. Detect only the exact
 * Lua 5.3 header here; other malformed or incompatible bytecode still reaches
 * luaL_loadbufferx and is reported as a script failure. */
static bool mod_script_targets_other_size_t_width(const char *path, unsigned *target_width) {
    static const unsigned char lua53_header_prefix[] = {
        0x1b, 'L', 'u', 'a', 0x53, 0x00, 0x19, 0x93, 0x0d, 0x0a, 0x1a, 0x0a
    };
    unsigned char header[14];
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    size_t count = fread(header, 1, sizeof(header), file);
    fclose(file);
    if (count != sizeof(header) || memcmp(header, lua53_header_prefix, sizeof(lua53_header_prefix)) != 0)
        return false;
    unsigned width = header[13]; /* sizeof(size_t) in the Lua 5.3 bytecode header */
    if ((width != 4 && width != 8) || width == sizeof(size_t)) return false;
    if (target_width) *target_width = width;
    return true;
}

static void register_api_function(const char *name, lua_CFunction fn) {
    lua_pushcfunction(sLua, fn); lua_setglobal(sLua, name);
}

static void register_metatable(const char *name, lua_CFunction index, lua_CFunction newindex) {
    luaL_newmetatable(sLua, name);
    lua_pushcfunction(sLua, index); lua_setfield(sLua, -2, "__index");
    if (newindex) { lua_pushcfunction(sLua, newindex); lua_setfield(sLua, -2, "__newindex"); }
    lua_pushstring(sLua, name); lua_setfield(sLua, -2, "__metatable");
    lua_pop(sLua, 1);
}

static void expose_int(const char *name, lua_Integer value) {
    lua_pushinteger(sLua, value); lua_setglobal(sLua, name);
}

static void expose_character_sound_constants(void) {
    static const char *const names[] = {
        "CHAR_SOUND_YAH_WAH_HOO", "CHAR_SOUND_HOOHOO", "CHAR_SOUND_YAHOO", "CHAR_SOUND_UH",
        "CHAR_SOUND_HRMM", "CHAR_SOUND_WAH2", "CHAR_SOUND_WHOA", "CHAR_SOUND_EEUH",
        "CHAR_SOUND_ATTACKED", "CHAR_SOUND_OOOF", "CHAR_SOUND_OOOF2", "CHAR_SOUND_HERE_WE_GO",
        "CHAR_SOUND_YAWNING", "CHAR_SOUND_SNORING1", "CHAR_SOUND_SNORING2", "CHAR_SOUND_WAAAOOOW",
        "CHAR_SOUND_HAHA", "CHAR_SOUND_HAHA_2", "CHAR_SOUND_UH2", "CHAR_SOUND_UH2_2",
        "CHAR_SOUND_ON_FIRE", "CHAR_SOUND_DYING", "CHAR_SOUND_PANTING_COLD", "CHAR_SOUND_PANTING",
        "CHAR_SOUND_COUGHING1", "CHAR_SOUND_COUGHING2", "CHAR_SOUND_COUGHING3", "CHAR_SOUND_PUNCH_YAH",
        "CHAR_SOUND_PUNCH_HOO", "CHAR_SOUND_MAMA_MIA", "CHAR_SOUND_GROUND_POUND_WAH", "CHAR_SOUND_DROWNING",
        "CHAR_SOUND_PUNCH_WAH", "CHAR_SOUND_YAHOO_WAHA_YIPPEE", "CHAR_SOUND_DOH", "CHAR_SOUND_GAME_OVER",
        "CHAR_SOUND_HELLO", "CHAR_SOUND_PRESS_START_TO_PLAY", "CHAR_SOUND_TWIRL_BOUNCE", "CHAR_SOUND_SNORING3",
        "CHAR_SOUND_SO_LONGA_BOWSER", "CHAR_SOUND_IMA_TIRED", "CHAR_SOUND_LETS_A_GO", "CHAR_SOUND_OKEY_DOKEY"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        expose_int(names[i], (lua_Integer)i);
    expose_int("CHAR_SOUND_MAX", sizeof(names) / sizeof(names[0]));
}

static void expose_sequence_constants(void) {
    static const struct { const char *name; lua_Integer value; } constants[] = {
        { "SEQ_SOUND_PLAYER", SEQ_SOUND_PLAYER },
        { "SEQ_EVENT_CUTSCENE_COLLECT_STAR", SEQ_EVENT_CUTSCENE_COLLECT_STAR },
        { "SEQ_MENU_TITLE_SCREEN", SEQ_MENU_TITLE_SCREEN },
        { "SEQ_LEVEL_GRASS", SEQ_LEVEL_GRASS },
        { "SEQ_LEVEL_INSIDE_CASTLE", SEQ_LEVEL_INSIDE_CASTLE },
        { "SEQ_LEVEL_WATER", SEQ_LEVEL_WATER },
        { "SEQ_LEVEL_HOT", SEQ_LEVEL_HOT },
        { "SEQ_LEVEL_BOSS_KOOPA", SEQ_LEVEL_BOSS_KOOPA },
        { "SEQ_LEVEL_SNOW", SEQ_LEVEL_SNOW },
        { "SEQ_LEVEL_SLIDE", SEQ_LEVEL_SLIDE },
        { "SEQ_LEVEL_SPOOKY", SEQ_LEVEL_SPOOKY },
        { "SEQ_EVENT_PIRANHA_PLANT", SEQ_EVENT_PIRANHA_PLANT },
        { "SEQ_LEVEL_UNDERGROUND", SEQ_LEVEL_UNDERGROUND },
        { "SEQ_MENU_STAR_SELECT", SEQ_MENU_STAR_SELECT },
        { "SEQ_EVENT_POWERUP", SEQ_EVENT_POWERUP },
        { "SEQ_EVENT_METAL_CAP", SEQ_EVENT_METAL_CAP },
        { "SEQ_EVENT_KOOPA_MESSAGE", SEQ_EVENT_KOOPA_MESSAGE },
        { "SEQ_LEVEL_KOOPA_ROAD", SEQ_LEVEL_KOOPA_ROAD },
        { "SEQ_EVENT_HIGH_SCORE", SEQ_EVENT_HIGH_SCORE },
        { "SEQ_EVENT_MERRY_GO_ROUND", SEQ_EVENT_MERRY_GO_ROUND },
        { "SEQ_EVENT_RACE", SEQ_EVENT_RACE },
        { "SEQ_EVENT_CUTSCENE_STAR_SPAWN", SEQ_EVENT_CUTSCENE_STAR_SPAWN },
        { "SEQ_EVENT_BOSS", SEQ_EVENT_BOSS },
        { "SEQ_EVENT_CUTSCENE_COLLECT_KEY", SEQ_EVENT_CUTSCENE_COLLECT_KEY },
        { "SEQ_EVENT_ENDLESS_STAIRS", SEQ_EVENT_ENDLESS_STAIRS },
        { "SEQ_LEVEL_BOSS_KOOPA_FINAL", SEQ_LEVEL_BOSS_KOOPA_FINAL },
        { "SEQ_EVENT_CUTSCENE_CREDITS", SEQ_EVENT_CUTSCENE_CREDITS },
        { "SEQ_EVENT_SOLVE_PUZZLE", SEQ_EVENT_SOLVE_PUZZLE },
        { "SEQ_EVENT_TOAD_MESSAGE", SEQ_EVENT_TOAD_MESSAGE },
        { "SEQ_EVENT_PEACH_MESSAGE", SEQ_EVENT_PEACH_MESSAGE },
        { "SEQ_EVENT_CUTSCENE_INTRO", SEQ_EVENT_CUTSCENE_INTRO },
        { "SEQ_EVENT_CUTSCENE_VICTORY", SEQ_EVENT_CUTSCENE_VICTORY },
        { "SEQ_EVENT_CUTSCENE_ENDING", SEQ_EVENT_CUTSCENE_ENDING },
        { "SEQ_MENU_FILE_SELECT", SEQ_MENU_FILE_SELECT },
        { "SEQ_EVENT_CUTSCENE_LAKITU", SEQ_EVENT_CUTSCENE_LAKITU },
        { "SEQ_COUNT", SEQ_COUNT },
        { "SEQ_VARIATION", SEQ_VARIATION },
    };
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); ++i)
        expose_int(constants[i].name, constants[i].value);
}

static void expose_mario_animation_constants(void) {
    static const struct { const char *name; lua_Integer value; } constants[] = {
#include "mod_mario_animation_constants.inc"
    };
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); ++i)
        expose_int(constants[i].name, constants[i].value);
}

static void expose_string(const char *name, const char *value) {
    lua_pushstring(sLua, value); lua_setglobal(sLua, name);
}

bool mod_runtime_stay_in_level_after_star(void) {
    Mod *latest = NULL;
    for (Mod *mod = sMods; mod; mod = mod->next) {
        if (mod->enabled && mod->star_exit_setting_order &&
            (!latest || mod->star_exit_setting_order > latest->star_exit_setting_order))
            latest = mod;
    }
    return latest && latest->stay_in_level_after_star;
}

static const char *const sLevelSettingNames[MOD_LEVEL_VALUE_COUNT] = {
    "fixVanishFloors", "starHeal", "mushroom1UpHeal", "previewBlueCoins",
    "respawnBlueCoinsSwitch", "visibleSecrets", "showStarNumber",
    "extendedPauseDisplay", "pauseExitAnywhere", "cellHeightLimit", "coinsRequiredForCoinStar"
};

static int32_t level_value_default(ModRuntimeLevelValue value) {
    /* The current native collision path already lets Mario pass vanish-cap
     * walls while invisible; retain that behavior until a mod overrides it. */
    if (value == MOD_LEVEL_VALUE_FIX_VANISH_FLOORS) return 1;
    if (value == MOD_LEVEL_VALUE_CELL_HEIGHT_LIMIT) return 20000;
    if (value == MOD_LEVEL_VALUE_COINS_REQUIRED_FOR_COIN_STAR) return 100;
    return 0;
}

int32_t mod_runtime_level_value(ModRuntimeLevelValue value) {
    if (value < 0 || value >= MOD_LEVEL_VALUE_COUNT) return false;
    Mod *latest = NULL;
    for (Mod *mod = sMods; mod; mod = mod->next) {
        if (mod->enabled && mod->level_setting_order[value] &&
            (!latest || mod->level_setting_order[value] > latest->level_setting_order[value]))
            latest = mod;
    }
    return latest ? latest->level_setting_value[value] : level_value_default(value);
}

static int level_value_index(lua_State *L) {
    const char *key = luaL_checkstring(L, 2);
    if (!strncmp(key, "fixCollisionBugs", sizeof("fixCollisionBugs") - 1)) {
        if (sCurrentMod && !sCurrentMod->collision_fix_unavailable_warned) {
            sCurrentMod->collision_fix_unavailable_warned = true;
            mod_log("warning", sCurrentMod->name,
                    "CoopDX fixCollisionBugs flags have no native collision-pipeline adapter; exposing nil so mods can use their upstream optional-feature fallback");
        }
        lua_pushnil(L);
        return 1;
    }
    for (int i = 0; i < MOD_LEVEL_VALUE_COUNT; ++i) {
        if (!strcmp(key, sLevelSettingNames[i])) {
            lua_pushinteger(L, mod_runtime_level_value((ModRuntimeLevelValue)i));
            return 1;
        }
    }
    return luaL_error(L, "gLevelValues.%s has no native adapter", key);
}

static int level_value_newindex(lua_State *L) {
    const char *key = luaL_checkstring(L, 2);
    if (!strncmp(key, "fixCollisionBugs", sizeof("fixCollisionBugs") - 1)) {
        if (sCurrentMod && !sCurrentMod->collision_fix_unavailable_warned) {
            sCurrentMod->collision_fix_unavailable_warned = true;
            mod_log("warning", sCurrentMod->name,
                    "CoopDX fixCollisionBugs flags have no native collision-pipeline adapter; values are unavailable and writes are ignored");
        }
        return 0;
    }
    int field = -1;
    for (int i = 0; i < MOD_LEVEL_VALUE_COUNT; ++i)
        if (!strcmp(key, sLevelSettingNames[i])) { field = i; break; }
    if (field < 0) return luaL_error(L, "gLevelValues.%s has no native adapter", key);
    int32_t value;
    if (lua_type(L, 3) == LUA_TBOOLEAN) {
        value = lua_toboolean(L, 3) ? 1 : 0;
    } else {
        int is_integer = 0;
        lua_Integer integer = lua_tointegerx(L, 3, &is_integer);
        bool is_s16 = field == MOD_LEVEL_VALUE_CELL_HEIGHT_LIMIT ||
                      field == MOD_LEVEL_VALUE_COINS_REQUIRED_FOR_COIN_STAR;
        lua_Integer minimum = is_s16 ? INT16_MIN : 0;
        lua_Integer maximum = is_s16 ? INT16_MAX : UINT8_MAX;
        if (!is_integer || integer < minimum || integer > maximum)
            return luaL_argerror(L, 3, is_s16
                                 ? "s16 level setting expects an integer from -32768 through 32767"
                                 : "u8 level setting expects an integer from 0 through 255");
        value = (int32_t)integer;
    }
    if (!sCurrentMod) return luaL_error(L, "gLevelValues writes require an active mod");
    sCurrentMod->level_setting_order[field] = ++sLevelSettingOrder;
    sCurrentMod->level_setting_value[field] = value;
    if (field == MOD_LEVEL_VALUE_EXTENDED_PAUSE_DISPLAY && value != 0 &&
        !sCurrentMod->level_setting_warned[field]) {
        sCurrentMod->level_setting_warned[field] = true;
        mod_log("warning", sCurrentMod->name,
                "gLevelValues.extendedPauseDisplay is retained, but the CoopDX extended pause renderer is unavailable");
    }
    if (field == MOD_LEVEL_VALUE_SHOW_STAR_NUMBER && value != 0 &&
        !sCurrentMod->level_setting_warned[field]) {
        sCurrentMod->level_setting_warned[field] = true;
        mod_log("warning", sCurrentMod->name,
                "gLevelValues.showStarNumber is retained, but this build has no star-number actor renderer");
    }
    return 0;
}

static int server_settings_index(lua_State *L) {
    const char *key = luaL_checkstring(L, 2);
    if (!strcmp(key, "stayInLevelAfterStar")) {
        lua_pushinteger(L, mod_runtime_stay_in_level_after_star() ? 1 : 0);
        return 1;
    }
    if (!strcmp(key, "playerInteractions") || !strcmp(key, "playerKnockbackStrength") ||
        !strcmp(key, "bubbleDeath")) {
        if (sCurrentMod && !sCurrentMod->multiplayer_settings_warned) {
            sCurrentMod->multiplayer_settings_warned = true;
            mod_log("warning", sCurrentMod->name,
                    "gServerSettings multiplayer fields expose CoopDX single-player defaults; multiplayer effects are unavailable");
        }
        if (!strcmp(key, "playerInteractions")) lua_pushinteger(L, 1); /* PLAYER_INTERACTIONS_SOLID */
        else if (!strcmp(key, "playerKnockbackStrength")) lua_pushinteger(L, 25);
        else lua_pushinteger(L, 1);
        return 1;
    }
    return luaL_error(L, "gServerSettings.%s needs an engine adapter", key);
}

static int server_settings_newindex(lua_State *L) {
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "stayInLevelAfterStar"))
        return luaL_error(L, "gServerSettings.%s needs an engine adapter", key);
    lua_Integer value = luaL_checkinteger(L, 3);
    if (value != 0 && value != 1)
        return luaL_argerror(L, 3, "only STAR_EXIT (0) and STAR_STAY_IN_LEVEL (1) are supported");
    if (!sCurrentMod) return luaL_error(L, "gServerSettings writes require an active mod");
    sCurrentMod->star_exit_setting_order = ++sStarExitSettingOrder;
    sCurrentMod->stay_in_level_after_star = value == 1;
    return 0;
}

static void register_api(void) {
    register_metatable("sm64.mod.MarioState", mario_index, mario_newindex);
    register_metatable("sm64.mod.MarioBodyState", mario_body_state_index, mario_body_state_newindex);
    register_metatable("sm64.mod.PlayerCameraState", player_camera_state_index, player_camera_state_newindex);
    register_metatable("sm64.mod.Camera", camera_index, camera_newindex);
    register_metatable("sm64.mod.Controller", controller_index, controller_newindex);
    register_metatable("sm64.mod.Vec3f", vector_index, vector_newindex);
    register_metatable("sm64.mod.Vec3s", vector_index, vector_newindex);
    register_metatable("sm64.mod.Object", object_index, object_newindex);
    luaL_getmetatable(sLua, "sm64.mod.Object");
    lua_pushcfunction(sLua, object_equal);
    lua_setfield(sLua, -2, "__eq");
    lua_pop(sLua, 1);
    register_metatable("sm64.mod.Pointer", pointer_index, NULL);
    luaL_getmetatable(sLua, "sm64.mod.Pointer");
    lua_pushcfunction(sLua, pointer_equal);
    lua_setfield(sLua, -2, "__eq");
    lua_pop(sLua, 1);
    register_metatable("sm64.mod.ObjectHeader", object_header_index, NULL);
    register_metatable("sm64.mod.ObjectGfx", object_gfx_index, object_gfx_newindex);
    register_metatable("sm64.mod.AnimationInfo", animation_info_index, animation_info_newindex);
    register_metatable("sm64.mod.Animation", animation_index, NULL);
    register_metatable("sm64.mod.Character", character_index, NULL);
    register_metatable("sm64.mod.GraphNode", graph_node_index, graph_node_newindex);
    register_metatable("sm64.mod.GraphNodeObject", graph_node_object_index, graph_node_object_newindex);
    register_metatable("sm64.mod.GfxOffset", gfx_offset_index, gfx_offset_newindex);
    register_metatable("sm64.mod.Surface", surface_index, surface_newindex);
    register_metatable("sm64.mod.ServerSettings", server_settings_index, server_settings_newindex);
    register_metatable("sm64.mod.Area", area_index, area_newindex);
    register_metatable("sm64.mod.LakituState", lakitu_state_index, lakitu_state_newindex);
    register_metatable("sm64.mod.LevelValues", level_value_index, level_value_newindex);
    lua_newuserdata(sLua, 1);
    luaL_getmetatable(sLua, "sm64.mod.LevelValues");
    lua_setmetatable(sLua, -2);
    lua_setglobal(sLua, "gLevelValues");
    lua_newuserdata(sLua, 1);
    luaL_getmetatable(sLua, "sm64.mod.ServerSettings");
    lua_setmetatable(sLua, -2);
    lua_setglobal(sLua, "gServerSettings");
    expose_int("STAR_EXIT", 0);
    expose_int("STAR_STAY_IN_LEVEL", 1);
    expose_int("STAR_NON_STOP", 2);
    expose_int("PLAYER_INTERACTIONS_NONE", 0);
    expose_int("PLAYER_INTERACTIONS_SOLID", 1);
    expose_int("PLAYER_INTERACTIONS_PVP", 2);
    expose_int("UNKNOWN_GLOBAL_INDEX", UINT8_MAX);
    /* CoopDX CharacterType values; this engine currently exposes only native Mario. */
    expose_int("CT_MARIO", 0); expose_int("CT_LUIGI", 1); expose_int("CT_TOAD", 2);
    expose_int("CT_WALUIGI", 3); expose_int("CT_WARIO", 4); expose_int("CT_MAX", 5);
    expose_int("UNKNOWN_LOCAL_INDEX", UINT8_MAX);
    expose_int("ROTATION_PIVOT_X_LEFT", 0);
    expose_int("ROTATION_PIVOT_X_RIGHT", 1);
    expose_int("ROTATION_PIVOT_Y_TOP", 0);
    expose_int("ROTATION_PIVOT_Y_BOTTOM", 1);
    lua_pushnumber(sLua, 0.5); lua_setglobal(sLua, "ROTATION_PIVOT_X_CENTER");
    lua_pushnumber(sLua, 0.5); lua_setglobal(sLua, "ROTATION_PIVOT_Y_CENTER");
    /* Mission 69's compatibility reference is pinned to CoopDX v1.5.1. */
    expose_string("SM64COOPDX_VERSION", "v1.5.1");
    register_api_function("hook_event", l_hook_event);
    register_api_function("camera_config_is_free_cam_enabled", l_camera_config_is_free_cam_enabled);
    register_api_function("camera_config_is_x_inverted", l_camera_config_is_x_inverted);
    register_api_function("camera_config_is_y_inverted", l_camera_config_is_y_inverted);
    register_api_function("hook_on_sync_table_change", l_hook_on_sync_table_change);
    register_api_function("allocate_mario_action", l_allocate_mario_action);
    register_api_function("hook_mario_action", l_hook_mario_action);
    register_api_function("hook_behavior", l_hook_behavior);
    register_api_function("spawn_object", l_spawn_object);
    register_api_function("spawn_object_abs_with_rot", l_spawn_object_abs_with_rot);
    register_api_function("spawn_non_sync_object", l_spawn_non_sync_object);
    register_api_function("spawn_sync_object", l_spawn_sync_object_unsupported);
    register_api_function("get_id_from_behavior", l_get_id_from_behavior);
    register_api_function("get_behavior_from_id", l_get_behavior_from_id);
    register_api_function("get_behavior_name_from_id", l_get_behavior_name_from_id);
    register_api_function("cur_obj_init_animation", l_cur_obj_init_animation);
    register_api_function("set_object_respawn_info_bits", l_set_object_respawn_info_bits);
    register_api_function("load_object_collision_model", l_load_object_collision_model);
    register_api_function("obj_spawn_loot_yellow_coins", l_obj_spawn_loot_yellow_coins);
    register_api_function("get_object_list_from_behavior", l_get_object_list_from_behavior);
    register_api_function("smlua_model_util_get_id", l_smlua_model_util_get_id);
    register_api_function("smlua_collision_util_get", l_smlua_collision_util_get);
    register_api_function("obj_get_first", l_obj_get_first);
    register_api_function("obj_get_next", l_obj_get_next);
    register_api_function("obj_get_first_with_behavior_id", l_obj_get_first_with_behavior_id);
    register_api_function("obj_get_nearest_object_with_behavior_id",
                           l_obj_get_nearest_object_with_behavior_id);
    register_api_function("obj_get_first_with_behavior_id_and_field_s32",
                           l_obj_get_first_with_behavior_id_and_field_s32);
    register_api_function("obj_get_field_u32", l_obj_get_field_u32);
    register_api_function("obj_set_field_u32", l_obj_set_field_u32);
    register_api_function("obj_get_next_with_same_behavior_id", l_obj_get_next_with_same_behavior_id);
    register_api_function("obj_mark_for_deletion", l_obj_mark_for_deletion);
    register_api_function("obj_has_behavior_id", l_obj_has_behavior_id);
    register_api_function("obj_is_grabbable", l_obj_is_grabbable);
    register_api_function("obj_angle_to_object", l_obj_angle_to_object);
    register_api_function("obj_is_mushroom_1up", l_obj_is_mushroom_1up);
    register_api_function("obj_check_hitbox_overlap", l_obj_check_hitbox_overlap);
    register_api_function("obj_is_valid_for_interaction", l_obj_is_valid_for_interaction);
    register_api_function("obj_is_attackable", l_obj_is_attackable);
    register_api_function("obj_is_bully", l_obj_is_bully);
    register_api_function("obj_is_coin", l_obj_is_coin);
    register_api_function("obj_is_exclamation_box", l_obj_is_exclamation_box);
    register_api_function("obj_is_secret", l_obj_is_secret);
    register_api_function("obj_has_model_extended", l_obj_has_model_extended);
    register_api_function("obj_set_model_extended", l_obj_set_model_extended);
    register_api_function("obj_scale_xyz", l_obj_scale_xyz);
    register_api_function("obj_scale", l_obj_scale);
    register_api_function("obj_set_pos", l_obj_set_pos);
    register_api_function("obj_set_angle", l_obj_set_angle);
    register_api_function("obj_set_face_angle", l_obj_set_face_angle);
    register_api_function("obj_set_vel", l_obj_set_vel);
    register_api_function("obj_move_xyz", l_obj_move_xyz);
    register_api_function("find_ceil_height", l_find_ceil_height);
    register_api_function("find_floor_height", l_find_floor_height);
    register_api_function("find_water_level", l_find_water_level);
    register_api_function("obj_scale_random", l_obj_scale_random);
    register_api_function("set_mario_action", l_set_mario_action);
    register_api_function("perform_ground_step", l_perform_ground_step);
    register_api_function("stationary_ground_step", l_stationary_ground_step);
    register_api_function("perform_air_step", l_perform_air_step);
    register_api_function("mario_bonk_reflection", l_mario_bonk_reflection);
    register_api_function("mario_get_floor_class", l_mario_get_floor_class);
    register_api_function("perform_water_step", l_perform_water_step);
    register_api_function("level_trigger_warp", l_level_trigger_warp);
    register_api_function("mario_grab_used_object", l_mario_grab_used_object);
    register_api_function("mario_stop_riding_object", l_mario_stop_riding_object);
    register_api_function("mario_stop_riding_and_holding", l_mario_stop_riding_and_holding);
    register_api_function("mario_set_bubbled", l_mario_set_bubbled_unsupported);
    register_api_function("mario_can_bubble", l_mario_can_bubble);
    register_api_function("interact_coin", l_interact_coin);
    luaL_newmetatable(sLua, "sm64.mod.AudioSample");
    lua_pushliteral(sLua, "AudioSample");
    lua_setfield(sLua, -2, "__metatable");
    lua_pop(sLua, 1);
    luaL_newmetatable(sLua, "sm64.mod.AudioStream");
    lua_pushliteral(sLua, "AudioStream");
    lua_setfield(sLua, -2, "__metatable");
    lua_pop(sLua, 1);
    register_api_function("audio_sample_load", l_audio_sample_load);
    register_api_function("audio_sample_play", l_audio_sample_play);
    register_api_function("audio_sample_stop", l_audio_sample_stop);
    register_api_function("audio_sample_destroy", l_audio_sample_destroy);
    register_api_function("audio_stream_load", l_audio_stream_load);
    register_api_function("audio_stream_play", l_audio_stream_play);
    register_api_function("audio_stream_set_looping", l_audio_stream_set_looping);
    register_api_function("audio_stream_stop", l_audio_stream_stop);
    register_api_function("audio_stream_destroy", l_audio_stream_destroy);
    register_api_function("smlua_audio_utils_replace_sequence", l_smlua_audio_utils_replace_sequence);
    register_api_function("play_sound", l_play_sound);
    register_api_function("play_mario_heavy_landing_sound", l_play_mario_heavy_landing_sound);
    register_api_function("play_mario_action_sound", l_play_mario_action_sound);
    register_api_function("play_mario_sound", l_play_mario_sound);
    register_api_function("play_course_clear", l_play_course_clear);
    register_api_function("play_flip_sounds", l_play_flip_sounds);
    register_api_function("network_send_object", l_network_send_object_single_player);
    register_api_function("network_send", l_network_send_unsupported);
    register_api_function("network_is_server", l_network_is_server);
    register_api_function("network_get_player_text_color_string", l_network_get_player_text_color_string);
    lua_pushliteral(sLua, "network_global_index_from_local");
    lua_pushcclosure(sLua, l_network_index_single_player, 1);
    lua_setglobal(sLua, "network_global_index_from_local");
    lua_pushliteral(sLua, "network_local_index_from_global");
    lua_pushcclosure(sLua, l_network_index_single_player, 1);
    lua_setglobal(sLua, "network_local_index_from_global");
    register_api_function("djui_popup_create", l_djui_popup_create);
    register_api_function("djui_chat_message_create", l_djui_chat_message_create);
    register_api_function("djui_hud_set_resolution", l_djui_hud_set_resolution);
    register_api_function("djui_hud_set_font", l_djui_hud_set_font);
    register_api_function("djui_hud_get_screen_width", l_djui_hud_get_screen_width);
    register_api_function("djui_hud_get_screen_height", l_djui_hud_get_screen_height);
    register_api_function("djui_hud_set_color", l_djui_hud_set_color);
    register_api_function("djui_hud_set_rotation", l_djui_hud_set_rotation);
    register_api_function("djui_hud_reset_color", l_djui_hud_reset_color);
    register_api_function("djui_hud_measure_text", l_djui_hud_measure_text);
    register_api_function("djui_hud_print_text", l_djui_hud_print_text);
    register_api_function("atan2s", l_atan2s);
    register_api_function("sins", l_sins);
    register_api_function("coss", l_coss);
    register_api_function("approach_s32", l_approach_s32);
    register_api_function("vec3f_set", l_vec3f_set);
    register_api_function("vec3f_copy", l_vec3f_copy);
    register_api_function("vec3f_normalize", l_vec3f_normalize);
    register_api_function("vec3f_dist", l_vec3f_dist);
    register_api_function("collision_find_surface_on_ray", l_collision_find_surface_on_ray);
    register_api_function("djui_hud_render_rect", l_djui_hud_render_rect);
    register_api_function("djui_hud_render_texture", l_djui_hud_render_texture);
    register_api_function("djui_hud_render_texture_interpolated", l_djui_hud_render_texture_interpolated);
    register_api_function("djui_hud_get_mouse_x", l_djui_hud_pointer_unavailable);
    register_api_function("djui_hud_get_mouse_y", l_djui_hud_pointer_unavailable);
    register_api_function("djui_hud_get_mouse_buttons_pressed", l_djui_hud_pointer_unavailable);
    register_api_function("djui_hud_is_pause_menu_created", l_djui_hud_pause_menu_created);
    register_api_function("hud_hide", l_hud_hide);
    register_api_function("hud_show", l_hud_show);
    register_api_function("hud_is_hidden", l_hud_is_hidden);
    register_api_function("hud_get_value", l_hud_get_value);
    register_api_function("hud_set_value", l_hud_set_value);
    register_api_function("get_texture_info", l_get_texture_info);
    register_api_function("is_game_paused", l_is_game_paused);
    register_api_function("mario_set_forward_vel", l_mario_set_forward_vel);
    register_api_function("set_mario_animation", l_set_mario_animation);
    register_api_function("set_mario_anim_with_accel", l_set_mario_anim_with_accel);
    register_api_function("set_character_anim_with_accel", l_set_character_anim_with_accel);
    register_api_function("set_anim_to_frame", l_set_anim_to_frame);
    register_api_function("smlua_anim_util_register_animation", l_smlua_anim_util_register_animation);
    register_api_function("smlua_anim_util_set_animation", l_smlua_anim_util_set_animation);
    register_api_function("require", l_mod_require);
    register_api_function("mod_storage_load", l_mod_storage_load);
    register_api_function("mod_storage_save", l_mod_storage_save);
    register_api_function("hook_mod_menu_button", l_hook_mod_menu_button);
    register_api_function("hook_mod_menu_checkbox", l_hook_mod_menu_checkbox);
    register_api_function("hook_mod_menu_slider", l_hook_mod_menu_slider);
    register_api_function("hook_chat_command", l_hook_chat_command);
    register_api_function("get_current_save_file_num", l_get_current_save_file_num);
    register_api_function("level_is_vanilla_level", l_level_is_vanilla_level);
    register_api_function("level_script_parse", l_level_script_parse);
    register_api_function("get_star_name_ascii", l_get_star_name_ascii);
    register_api_function("get_level_name_ascii", l_get_level_name_ascii);
    register_api_function("play_character_sound_offset", l_play_character_sound_offset);
    register_api_function("play_character_sound", l_play_character_sound);
    register_api_function("play_character_sound_if_no_flag", l_play_character_sound_if_no_flag);
    register_api_function("set_camera_mode", l_set_camera_mode);
    register_api_function("set_camera_shake_from_hit", l_set_camera_shake_from_hit);
    register_api_function("set_camera_shake_from_point", l_set_camera_shake_from_point);
    register_api_function("play_transition", l_play_transition);
    register_api_function("play_cap_music", l_play_cap_music);
    register_api_function("play_shell_music", l_play_shell_music);
    register_api_function("stop_shell_music", l_stop_shell_music);
    register_api_function("save_file_get_flags", l_save_file_get_flags);
    register_api_function("save_file_get_star_flags", l_save_file_get_star_flags);

    expose_int("HOOK_UPDATE", HOOK_UPDATE);
    expose_int("HOOK_MARIO_UPDATE", HOOK_MARIO_UPDATE);
    expose_int("HOOK_BEFORE_MARIO_UPDATE", HOOK_BEFORE_MARIO_UPDATE);
    expose_int("HOOK_ON_SET_MARIO_ACTION", HOOK_ON_SET_MARIO_ACTION);
    expose_int("HOOK_BEFORE_PHYS_STEP", HOOK_BEFORE_PHYS_STEP);
    expose_int("HOOK_ALLOW_PVP_ATTACK", HOOK_ALLOW_PVP_ATTACK);
    expose_int("HOOK_ON_PVP_ATTACK", HOOK_ON_PVP_ATTACK);
    expose_int("HOOK_ON_PLAYER_CONNECTED", HOOK_ON_PLAYER_CONNECTED);
    expose_int("HOOK_ON_PLAYER_DISCONNECTED", HOOK_ON_PLAYER_DISCONNECTED);
    expose_int("HOOK_BEFORE_SET_MARIO_ACTION", 30);
    expose_int("HOOK_ON_HUD_RENDER", HOOK_ON_HUD_RENDER);
    expose_int("HOOK_ON_HUD_RENDER_BEHIND", HOOK_ON_HUD_RENDER_BEHIND);
    expose_int("HOOK_ON_OBJECT_RENDER", HOOK_ON_OBJECT_RENDER);
    expose_int("HOOK_OBJECT_SET_MODEL", HOOK_OBJECT_SET_MODEL);
    expose_int("RESOLUTION_DJUI", 0);
    expose_int("RESOLUTION_N64", 1);
    expose_int("FONT_LEGACY", -1);
    expose_int("FONT_NORMAL", 0);
    expose_int("FONT_MENU", 1);
    expose_int("FONT_HUD", 2);
    expose_int("FONT_ALIASED", 3);
    expose_int("FONT_CUSTOM_HUD", 4);
    expose_int("FONT_RECOLOR_HUD", 5);
    expose_int("FONT_SPECIAL", 6);
    expose_int("HOOK_ALLOW_INTERACT", HOOK_ALLOW_INTERACT);
    expose_int("HOOK_ON_INTERACT", HOOK_ON_INTERACT);
    expose_int("HOOK_ON_DEATH", HOOK_ON_DEATH);
    expose_int("HOOK_ON_PAUSE_EXIT", HOOK_ON_PAUSE_EXIT);
    expose_int("HOOK_ON_PACKET_RECEIVE", HOOK_ON_PACKET_RECEIVE);
    expose_int("HOOK_ALLOW_HAZARD_SURFACE", HOOK_ALLOW_HAZARD_SURFACE);
    expose_int("HUD_DISPLAY_LIVES", 0);
    expose_int("HUD_DISPLAY_COINS", 1);
    expose_int("HUD_DISPLAY_STARS", 2);
    expose_int("HUD_DISPLAY_WEDGES", 3);
    expose_int("HUD_DISPLAY_KEYS", 4);
    expose_int("HUD_DISPLAY_FLAGS", 5);
    expose_int("HUD_DISPLAY_TIMER", 6);
    expose_int("HUD_DISPLAY_CAMERA_STATUS", 7);
    expose_int("HAZARD_TYPE_LAVA_FLOOR", 0);
    expose_int("HAZARD_TYPE_LAVA_WALL", 1);
    expose_int("HAZARD_TYPE_QUICKSAND", 2);
    expose_int("HAZARD_TYPE_HORIZONTAL_WIND", 3);
    expose_int("HAZARD_TYPE_VERTICAL_WIND", 4);
    expose_int("INT_GROUND_POUND_OR_TWIRL", INT_GROUND_POUND_OR_TWIRL);
    expose_int("INT_PUNCH", INT_PUNCH);
    expose_int("INT_KICK", INT_KICK);
    expose_int("INT_TRIP", INT_TRIP);
    expose_int("INT_SLIDE_KICK", INT_SLIDE_KICK);
    expose_int("INT_FAST_ATTACK_OR_SHELL", INT_FAST_ATTACK_OR_SHELL);
    expose_int("INT_HIT_FROM_ABOVE", INT_HIT_FROM_ABOVE);
    expose_int("INT_HIT_FROM_BELOW", INT_HIT_FROM_BELOW);
    expose_int("HOOK_ON_LEVEL_INIT", HOOK_ON_LEVEL_INIT);
    expose_int("HOOK_ON_WARP", HOOK_ON_WARP);
    expose_int("HOOK_USE_ACT_SELECT", HOOK_USE_ACT_SELECT);
    expose_int("HOOK_ON_SET_CAMERA_MODE", HOOK_ON_SET_CAMERA_MODE);
    expose_int("HOOK_ON_CHANGE_CAMERA_ANGLE", HOOK_ON_CHANGE_CAMERA_ANGLE);
    expose_int("CAM_ANGLE_MARIO", CAM_ANGLE_MARIO);
    expose_int("CAM_ANGLE_LAKITU", CAM_ANGLE_LAKITU);
    expose_int("HOOK_ON_EXIT", HOOK_ON_EXIT);
    expose_int("HOOK_MIRROR_MARIO_RENDER", HOOK_MIRROR_MARIO_RENDER);

    push_camera(sLua);
    lua_setglobal(sLua, "gCamera");
    push_lakitu_state(sLua);
    lua_setglobal(sLua, "gLakituState");

    expose_int("A_BUTTON", A_BUTTON); expose_int("B_BUTTON", B_BUTTON);
    expose_int("X_BUTTON", X_BUTTON); expose_int("Y_BUTTON", Y_BUTTON);
    expose_int("Z_TRIG", Z_TRIG); expose_int("START_BUTTON", START_BUTTON);
    expose_int("U_JPAD", U_JPAD); expose_int("D_JPAD", D_JPAD);
    expose_int("L_JPAD", L_JPAD); expose_int("R_JPAD", R_JPAD);
    expose_int("L_TRIG", L_TRIG); expose_int("R_TRIG", R_TRIG);
    expose_int("U_CBUTTONS", U_CBUTTONS); expose_int("D_CBUTTONS", D_CBUTTONS);
    expose_int("L_CBUTTONS", L_CBUTTONS); expose_int("R_CBUTTONS", R_CBUTTONS);
    expose_int("VALID_BUTTONS", VALID_BUTTONS); expose_int("C_BUTTONS", C_BUTTONS);
    expose_int("GRAPH_RENDER_ACTIVE", GRAPH_RENDER_ACTIVE);
    expose_int("GRAPH_RENDER_CHILDREN_FIRST", GRAPH_RENDER_CHILDREN_FIRST);
    expose_int("GRAPH_RENDER_BILLBOARD", GRAPH_RENDER_BILLBOARD);
    expose_int("GRAPH_RENDER_Z_BUFFER", GRAPH_RENDER_Z_BUFFER);
    expose_int("GRAPH_RENDER_INVISIBLE", GRAPH_RENDER_INVISIBLE);
    expose_int("GRAPH_RENDER_HAS_ANIMATION", GRAPH_RENDER_HAS_ANIMATION);
    expose_int("GRAPH_RENDER_CYLBOARD", GRAPH_RENDER_CYLBOARD);
    expose_mario_animation_constants();
    expose_int("ANIM_FLAG_NOLOOP", ANIM_FLAG_NOLOOP);
    expose_int("ANIM_FLAG_FORWARD", ANIM_FLAG_FORWARD);
    expose_int("ANIM_FLAG_2", ANIM_FLAG_2);
    expose_int("ANIM_FLAG_HOR_TRANS", ANIM_FLAG_HOR_TRANS);
    expose_int("ANIM_FLAG_VERT_TRANS", ANIM_FLAG_VERT_TRANS);
    expose_int("ANIM_FLAG_5", ANIM_FLAG_5);
    expose_int("ANIM_FLAG_6", ANIM_FLAG_6);
    expose_int("ANIM_FLAG_7", ANIM_FLAG_7);

    push_vector(sLua, LUA_UD_VEC_F, VEC_GLOBAL_SOUND_SOURCE);
    lua_setglobal(sLua, "gGlobalSoundSource");
    push_vector(sLua, LUA_UD_VEC_F, VEC_DEFAULT_SOUND_ARGS);
    lua_setglobal(sLua, "gDefaultSoundArgs");

    expose_int("STEP_TYPE_GROUND", MOD_RUNTIME_STEP_TYPE_GROUND);
    expose_int("STEP_TYPE_AIR", MOD_RUNTIME_STEP_TYPE_AIR);
    expose_int("STEP_TYPE_WATER", MOD_RUNTIME_STEP_TYPE_WATER);
    expose_int("STEP_TYPE_HANG", MOD_RUNTIME_STEP_TYPE_HANG);
    expose_int("GROUND_STEP_LEFT_GROUND", GROUND_STEP_LEFT_GROUND);
    expose_int("GROUND_STEP_NONE", GROUND_STEP_NONE);
    expose_int("GROUND_STEP_HIT_WALL", GROUND_STEP_HIT_WALL);
    expose_int("GROUND_STEP_HIT_WALL_STOP_QSTEPS", GROUND_STEP_HIT_WALL_STOP_QSTEPS);
    expose_int("GROUND_STEP_HIT_WALL_CONTINUE_QSTEPS", GROUND_STEP_HIT_WALL_CONTINUE_QSTEPS);
    expose_int("AIR_STEP_CHECK_LEDGE_GRAB", AIR_STEP_CHECK_LEDGE_GRAB);
    expose_int("AIR_STEP_CHECK_HANG", AIR_STEP_CHECK_HANG);
    expose_int("AIR_STEP_NONE", AIR_STEP_NONE);
    expose_int("AIR_STEP_LANDED", AIR_STEP_LANDED);
    expose_int("AIR_STEP_HIT_WALL", AIR_STEP_HIT_WALL);
    expose_int("AIR_STEP_GRABBED_LEDGE", AIR_STEP_GRABBED_LEDGE);
    expose_int("AIR_STEP_GRABBED_CEILING", AIR_STEP_GRABBED_CEILING);
    expose_int("AIR_STEP_HIT_LAVA_WALL", AIR_STEP_HIT_LAVA_WALL);
    expose_int("WATER_STEP_NONE", WATER_STEP_NONE);
    expose_int("WATER_STEP_HIT_FLOOR", WATER_STEP_HIT_FLOOR);
    expose_int("WATER_STEP_HIT_CEILING", WATER_STEP_HIT_CEILING);
    expose_int("WATER_STEP_CANCELLED", WATER_STEP_CANCELLED);
    expose_int("WATER_STEP_HIT_WALL", WATER_STEP_HIT_WALL);

    expose_int("OBJ_LIST_PLAYER", OBJ_LIST_PLAYER); expose_int("OBJ_LIST_GENACTOR", OBJ_LIST_GENACTOR);
    expose_int("OBJ_LIST_PUSHABLE", OBJ_LIST_PUSHABLE); expose_int("OBJ_LIST_LEVEL", OBJ_LIST_LEVEL);
    expose_int("OBJ_LIST_DEFAULT", OBJ_LIST_DEFAULT); expose_int("OBJ_LIST_SURFACE", OBJ_LIST_SURFACE);
    expose_int("OBJ_LIST_POLELIKE", OBJ_LIST_POLELIKE); expose_int("OBJ_LIST_SPAWNER", OBJ_LIST_SPAWNER);
    expose_int("OBJ_LIST_UNIMPORTANT", OBJ_LIST_UNIMPORTANT);

    expose_int("ACT_FLAG_CUSTOM_ACTION", ACT_FLAG_CUSTOM_ACTION);
    expose_int("ACT_INDEX_MASK", MOD_ACTION_INDEX_MASK);
    expose_int("ACT_NUM_ACTIONS_PER_GROUP", MOD_ACTIONS_PER_GROUP);
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
#include "mod_behavior_constants.inc"
#include "mod_model_constants.inc"
    expose_character_sound_constants();
    expose_sequence_constants();
    expose_int("INTERACT_PLAYER", (lua_Integer)(UINT32_C(1) << 31));

    /* Level and course identifiers are enums generated from the engine's
     * authoritative level/course lists, so they are not visible to the
     * preprocessor-based constant generator. Expose those exact enum values. */
#define STUB_LEVEL(_name, levelenum, courseenum, ...) \
    expose_int(#levelenum, levelenum); expose_int(#courseenum, courseenum);
#define DEFINE_LEVEL(_name, levelenum, courseenum, ...) \
    expose_int(#levelenum, levelenum); expose_int(#courseenum, courseenum);
#include "levels/level_defines.h"
#undef STUB_LEVEL
#undef DEFINE_LEVEL

#define DEFINE_COURSE(courseenum, _cutscenes) expose_int(#courseenum, courseenum);
#define DEFINE_COURSES_END()
#define DEFINE_BONUS_COURSE(courseenum, _cutscenes) expose_int(#courseenum, courseenum);
#include "levels/course_defines.h"
#undef DEFINE_COURSE
#undef DEFINE_COURSES_END
#undef DEFINE_BONUS_COURSE

    lua_newtable(sLua);
    for (int player = 0; player < 16; ++player) {
        push_mario_index(sLua, player);
        lua_rawseti(sLua, -2, player);
    }
    lua_setglobal(sLua, "gMarioStates");
    push_mario(sLua); lua_setglobal(sLua, "gMarioState");

    lua_newtable(sLua);
    lua_newtable(sLua); lua_rawseti(sLua, -2, 0);
    lua_setglobal(sLua, "gNetworkPlayers");
    lua_newtable(sLua);
    lua_newtable(sLua); lua_rawseti(sLua, -2, 0);
    lua_setglobal(sLua, "gPlayerSyncTable");
    lua_newtable(sLua); lua_setglobal(sLua, "gGlobalSyncTable");
    refresh_local_player_tables();

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
    /* CoopDX v1.5.1 mod bytecode still uses the legacy global max alias.
     * Keep that source-level compatibility by binding Lua's real math.max. */
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "max");
    lua_setglobal(L, "max");
    lua_pop(L, 1);
}

static bool load_mod(Mod *mod) {
    if (!mod) return false;
    pthread_mutex_lock(&sModListMutex);
    bool loaded = mod->loaded;
    pthread_mutex_unlock(&sModListMutex);
    if (loaded || mod->load_failed) return loaded;
    Mod *conflict = find_enabled_conflict(mod);
    if (conflict) {
        mod_logf("warning", mod->name, "cannot enable: incompatible with active mod '%s'", conflict->name);
        return false;
    }
    mod->network_send_warned = false;
    if (!mod->file_count) {
        mod->load_failed = true;
        mod_log("warning", mod->name, "mod contains no Lua scripts");
        return false;
    }
    /* CoopDX auto-runs root-level scripts in alphabetical path order. Nested
     * scripts stay available to require(), but are not startup entry points. */
    bool root_mod = !strcmp(mod->path, fs_get_write_path("mods"));
    bool found_main = root_mod;
    size_t root_scripts = 0;
    for (size_t i = 0; i < mod->file_count; ++i) {
        const char *relative = relative_path(mod, mod->files[i]);
        if (!relative || strchr(relative, '/')) continue;
        ++root_scripts;
        if (!strcmp(relative, "main.lua")) found_main = true;
    }
    if (!found_main && root_scripts != 1) {
        mod->load_failed = true;
        mod_log("warning", mod->name, "mod folder has no root-level main.lua");
        return false;
    }
    load_dynos_behaviors(mod);
    for (size_t i = 0; i < mod->file_count; ++i) {
        const char *relative = relative_path(mod, mod->files[i]);
        if (!relative || strchr(relative, '/')) continue;
        char script_path[SYS_MAX_PATH];
        unsigned target_width = 0;
        int script_path_length = snprintf(script_path, sizeof(script_path), "%s/%s", mod->path, relative);
        if (script_path_length > 0 && (size_t)script_path_length < sizeof(script_path) &&
            mod_script_targets_other_size_t_width(script_path, &target_width)) {
            mod_logf("warning", mod->name,
                     "skipping %s: Lua 5.3 bytecode targets %u-bit size_t; this runtime uses %u-bit size_t",
                     relative, target_width * 8, (unsigned)sizeof(size_t) * 8);
            continue;
        }
        if (!load_mod_file(mod, relative, false)) {
            mod_log("error", mod->name, "startup script failed; discarding registrations created by this mod");
            unload_mod(mod);
            mod->load_failed = true;
            return false;
        }
    }
    resolve_dynos_behavior_functions(mod);
    if (mod->contains_dynos_assets) {
        mod_log("warning", mod->name,
                "package contains DynOS resources: PNG-backed .tex, the bounded .bhv subset, and validated .bin actor models are active where supported; unsupported GeoLayout functions/opcodes, animation playback, .col collision, and other behavior commands remain unavailable");
    }
    pthread_mutex_lock(&sModListMutex);
    mod->loaded = true;
    pthread_mutex_unlock(&sModListMutex);
    mod_log("info", mod->name, "mod scripts loaded");
    return true;
}

static void unref_if_valid(int ref) { if (ref != LUA_NOREF && ref != LUA_REFNIL && sLua) luaL_unref(sLua, LUA_REGISTRYINDEX, ref); }
static void release_ref(int *ref) {
    if (!ref) return;
    unref_if_valid(*ref);
    *ref = LUA_NOREF;
}

static void clear_animation_reference(struct Object *object, struct Animation *animation) {
    if (object && object->header.gfx.unk38.curAnim == animation)
        object->header.gfx.unk38.curAnim = NULL;
}

static void clear_animation_references(struct Animation *animation) {
    if (!animation) return;
    if (gObjectLists) {
        for (int list = 0; list < NUM_OBJ_LISTS; ++list) {
            struct ObjectNode *head = &gObjectLists[list];
            struct ObjectNode *node = head->next;
            size_t visited = 0;
            while (node && node != head && visited++ < OBJECT_POOL_CAPACITY) {
                clear_animation_reference((struct Object *)node, animation);
                node = node->next;
            }
        }
    }
    if (gMarioState) clear_animation_reference(gMarioState->marioObj, animation);
}

static void clear_dynos_model_references(struct GraphNode *graph) {
    if (!graph) return;
    if (gObjectLists) {
        for (int list = 0; list < NUM_OBJ_LISTS; ++list) {
            struct ObjectNode *head = &gObjectLists[list];
            struct ObjectNode *node = head->next;
            size_t visited = 0;
            while (node && node != head && visited++ < OBJECT_POOL_CAPACITY) {
                struct Object *object = (struct Object *)node;
                if (object->header.gfx.sharedChild == graph)
                    object->header.gfx.sharedChild = NULL;
                node = node->next;
            }
        }
    }
    if (gMarioState && gMarioState->marioObj &&
        gMarioState->marioObj->header.gfx.sharedChild == graph)
        gMarioState->marioObj->header.gfx.sharedChild = NULL;
}

static void unload_mod(Mod *mod) {
    if (!mod) return;
    if (sHudHiddenOwner == mod) sHudHiddenOwner = NULL;
    remove_mod_popups(mod);
    restore_mod_visibility(mod);
    remove_render_hook_owners(mod, NULL);
    remove_native_behavior_states(mod, NULL);
    mod_audio_sequence_override_unload(mod);
    mod_audio_unload(mod);
    mod->star_exit_setting_order = 0;
    for (int i = 0; i < MOD_LEVEL_VALUE_COUNT; ++i) {
        mod->level_setting_order[i] = 0;
        mod->level_setting_warned[i] = false;
    }

    /* Stop scripted objects before freeing the callbacks they reference. */
    ModObject **object_link = &sObjects;
    while (*object_link) {
        ModObject *object = *object_link;
        if (object->owner == mod) {
            if (object->object) object->object->activeFlags = ACTIVE_FLAG_DEACTIVATED;
            *object_link = object->next;
            free(object);
        } else object_link = &object->next;
    }

    ModSyncHook **sync_hook_link = &sSyncHooks;
    while (*sync_hook_link) {
        ModSyncHook *hook = *sync_hook_link;
        if (hook->owner == mod) {
            *sync_hook_link = hook->next_global;
            unref_if_valid(hook->table_ref);
            unref_if_valid(hook->field_ref);
            unref_if_valid(hook->tag_ref);
            unref_if_valid(hook->function_ref);
            free(hook->file);
            free(hook);
            if (sSyncHookCount) --sSyncHookCount;
        } else sync_hook_link = &hook->next_global;
    }
    mod->sync_hooks = NULL;

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
            free(behavior->name);
            free(behavior->script);
            free_string_array(behavior->lua_tokens, behavior->lua_token_count);
            for (size_t i = 0; i < behavior->lua_token_count; ++i)
                unref_if_valid(behavior->lua_function_refs ? behavior->lua_function_refs[i] : LUA_NOREF);
            free(behavior->lua_function_refs);
            free(behavior);
        } else behavior_link = &behavior->next_global;
    }
    mod->behaviors = NULL;
    mod->dynos_behavior_count = 0;
    while (mod->models) {
        DynosModel *model = mod->models;
        mod->models = model->next_owned;
        if (sPlayerDynosModel == model) {
            sPlayerDynosModel = NULL;
            sPlayerDynosTraversalFrames = 0;
            if (gMarioState && gMarioState->marioObj && gLoadedGraphNodes && gLoadedGraphNodes[MODEL_MARIO])
                gMarioState->marioObj->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_MARIO];
        }
        DynosModel **global_link = &sDynosModels;
        while (*global_link && *global_link != model) global_link = &(*global_link)->next_global;
        if (*global_link == model) *global_link = model->next_global;
        clear_dynos_model_references(model->graph);
        if (gLoadedGraphNodes && gLoadedGraphNodes[model->native_slot] == model->graph)
            gLoadedGraphNodes[model->native_slot] = NULL;
        if (mod->dynos_model_bytes >= model->bundle.size) mod->dynos_model_bytes -= model->bundle.size;
        if (sDynosModelBytes >= model->bundle.size) sDynosModelBytes -= model->bundle.size;
        free(model->graph_pool);
        dynos_actor_native_free(&model->native);
        dynos_actor_free(&model->bundle);
        free(model->name);
        free(model);
    }
    while (mod->animations) {
        ModAnimation *animation = mod->animations;
        mod->animations = animation->next_owned;
        clear_animation_references(animation->animation);
        ModAnimation **global_link = &sAnimations;
        while (*global_link && *global_link != animation) global_link = &(*global_link)->next_global;
        if (*global_link == animation) *global_link = animation->next_global;
        if (sAnimationCount) --sAnimationCount;
        if (sAnimationBytes >= animation->bytes) sAnimationBytes -= animation->bytes;
        if (mod->animation_count) --mod->animation_count;
        if (mod->animation_bytes >= animation->bytes) mod->animation_bytes -= animation->bytes;
        free(animation->name);
        free(animation->values);
        free(animation->index);
        free(animation->animation);
        free(animation);
    }
    while (mod->textures) {
        ModTexture *texture = mod->textures;
        mod->textures = texture->next_owned;
        ModTexture **global_link = &sTextures;
        while (*global_link && *global_link != texture) global_link = &(*global_link)->next_global;
        if (*global_link == texture) *global_link = texture->next_global;
        if (sDynosTextureBytes >= texture->bytes) sDynosTextureBytes -= texture->bytes;
        if (mod->dynos_texture_bytes >= texture->bytes) mod->dynos_texture_bytes -= texture->bytes;
        stbi_image_free(texture->rgba32);
        free(texture->rgba16);
        free(texture->name);
        free(texture->file);
        free(texture);
    }
    mod->dynos_texture_count = 0;
    mod->dynos_texture_bytes = 0;
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
    pthread_mutex_lock(&sModListMutex);
    for (size_t i = 0; i < mod->menu_item_count; ++i) {
        unref_if_valid(mod->menu_items[i].callback_ref);
        free(mod->menu_items[i].file);
    }
    free(mod->menu_items); mod->menu_items = NULL;
    mod->menu_item_count = mod->menu_item_capacity = 0;
    mod->loaded = false;
    pthread_mutex_unlock(&sModListMutex);
    while (mod->commands) {
        ModCommand *next = mod->commands->next;
        unref_if_valid(mod->commands->callback_ref);
        free(mod->commands->file); free(mod->commands); mod->commands = next;
    }
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

static void call_hud_event(int event) {
    if (!sLua) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != event) continue;
        if (!call_ref(hook->owner, hook->file, hook->function_ref,
                      event == HOOK_ON_HUD_RENDER_BEHIND ? "HOOK_ON_HUD_RENDER_BEHIND" : "HOOK_ON_HUD_RENDER",
                      0, 0)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
        /* CoopDX resets transient HUD state after each callback while keeping
         * already queued draw commands intact. */
        mod_hud_reset_state();
    }
}

bool mod_runtime_on_death(struct MarioState *m) {
    if (!sLua || !m || m != current_mario()) return true;
    bool allow_death = true;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_DEATH) continue;
        push_mario(sLua);
        if (call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ON_DEATH", 1, 1)) {
            if (lua_isboolean(sLua, -1) && !lua_toboolean(sLua, -1)) allow_death = false;
            lua_pop(sLua, 1);
        } else {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
    return allow_death;
}

bool mod_runtime_on_pause_exit(bool used_exit_to_castle) {
    bool allow_exit = true;
    if (!sLua) return true;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_PAUSE_EXIT) continue;
        lua_pushboolean(sLua, used_exit_to_castle);
        if (call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ON_PAUSE_EXIT", 1, 1)) {
            if (lua_isboolean(sLua, -1) && !lua_toboolean(sLua, -1)) allow_exit = false;
            lua_pop(sLua, 1);
        } else {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
    return allow_exit;
}

bool mod_runtime_on_set_camera_mode(struct Camera *camera, int16_t mode, int16_t frames) {
    bool allow_set_mode = true;
    if (!sLua || !camera) return true;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_SET_CAMERA_MODE) continue;
        push_camera_reference(sLua, camera);
        lua_pushinteger(sLua, mode);
        lua_pushinteger(sLua, frames);
        if (call_ref(hook->owner, hook->file, hook->function_ref,
                     "HOOK_ON_SET_CAMERA_MODE", 3, 1)) {
            if (lua_isboolean(sLua, -1)) allow_set_mode = lua_toboolean(sLua, -1) != 0;
            lua_pop(sLua, 1);
        } else {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
    return allow_set_mode;
}

bool mod_runtime_allow_camera_angle(int32_t angle) {
    bool allow_set_angle = true;
    if (!sLua) return true;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_CHANGE_CAMERA_ANGLE) continue;
        lua_pushinteger(sLua, angle);
        if (call_ref(hook->owner, hook->file, hook->function_ref,
                     "HOOK_ON_CHANGE_CAMERA_ANGLE", 1, 1)) {
            if (lua_isboolean(sLua, -1)) allow_set_angle = lua_toboolean(sLua, -1) != 0;
            lua_pop(sLua, 1);
        } else {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
    return allow_set_angle;
}

bool mod_runtime_allow_hazard_surface(struct MarioState *m, int hazard_type) {
    bool allow_hazard = true;
    if (!sLua || !m || m != current_mario()) return true;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ALLOW_HAZARD_SURFACE) continue;
        push_mario(sLua);
        lua_pushinteger(sLua, hazard_type);
        if (call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ALLOW_HAZARD_SURFACE", 2, 1)) {
            if (lua_isboolean(sLua, -1) && !lua_toboolean(sLua, -1)) allow_hazard = false;
            lua_pop(sLua, 1);
        } else {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
    return allow_hazard;
}

void mod_runtime_on_warp(uint8_t warp_type, int16_t level, uint8_t area, uint8_t node, uint32_t arg) {
    if (!sLua) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_WARP) continue;
        lua_pushinteger(sLua, warp_type);
        lua_pushinteger(sLua, level);
        lua_pushinteger(sLua, area);
        lua_pushinteger(sLua, node);
        lua_pushinteger(sLua, arg);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ON_WARP", 5, 0)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
}

bool mod_runtime_use_act_select(int32_t level, bool *use_act_select) {
    if (!sLua || !use_act_select) return false;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_USE_ACT_SELECT) continue;
        lua_pushinteger(sLua, level);
        if (!call_ref(hook->owner, hook->file, hook->function_ref,
                      "HOOK_USE_ACT_SELECT", 1, 1)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
            continue;
        }
        if (lua_isboolean(sLua, -1)) {
            *use_act_select = lua_toboolean(sLua, -1) != 0;
            lua_pop(sLua, 1);
            return true;
        }
        lua_pop(sLua, 1);
    }
    return false;
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
    if (enabled) {
        pthread_mutex_lock(&sModListMutex);
        Mod *target = mod_runtime_find_index(index);
        Mod *conflict = target ? find_enabled_conflict(target) : NULL;
        pthread_mutex_unlock(&sModListMutex);
        if (conflict) return false;
    }
    return index >= 0 && enqueue_request(MOD_REQUEST_ENABLE, index, enabled, NULL);
}

bool mod_runtime_request_reload(int index) {
    return index >= 0 && enqueue_request(MOD_REQUEST_RELOAD, index, false, NULL);
}

bool mod_runtime_request_import_zip(const char *zip_path) {
    if (!zip_path || !*zip_path) return false;
    return enqueue_request(MOD_REQUEST_IMPORT_ZIP, 0, false, zip_path);
}

bool mod_runtime_request_player_dynos_model(const char *model_name) {
    if (!model_name || !*model_name || strlen(model_name) >= MOD_MAX_NAME ||
        !safe_relative_path(model_name) || strchr(model_name, '/')) return false;
    return enqueue_request(MOD_REQUEST_PLAYER_DYNOS_MODEL, 0, false, model_name);
}

bool mod_runtime_request_player_native_model(void) {
    return enqueue_request(MOD_REQUEST_PLAYER_NATIVE_MODEL, 0, false, NULL);
}

uint32_t mod_runtime_player_dynos_model_id(void) {
    DynosModel *model = sPlayerDynosModel;
    if (!model || !model->owner || !model->owner->enabled || !model->owner->loaded ||
        !gLoadedGraphNodes || gLoadedGraphNodes[model->native_slot] != model->graph) return 0;
    return model->extended_id;
}

bool mod_runtime_request_menu_set_value(int mod_index, int item_index, int64_t value) {
    if (mod_index < 0 || item_index < 0) return false;
    ModRuntimeMenuItem item_info;
    if (!mod_runtime_menu_get_item(mod_index, item_index, &item_info)) return false;
    if (item_info.type == MOD_RUNTIME_MENU_BUTTON) {
        if (value != 0) return false;
    } else if (item_info.type == MOD_RUNTIME_MENU_CHECKBOX) {
        if (value != 0 && value != 1) return false;
    } else if (value < item_info.minimum || value > item_info.maximum) return false;
    ModRequest *request = calloc(1, sizeof(*request));
    if (!request) return false;
    request->type = MOD_REQUEST_MENU_VALUE;
    request->item_index = item_index;
    request->value = value;
    pthread_mutex_lock(&sModListMutex);
    Mod *target = mod_runtime_find_index(mod_index);
    if (target) snprintf(request->target_name, sizeof(request->target_name), "%s", target->name);
    pthread_mutex_unlock(&sModListMutex);
    if (!request->target_name[0]) { free(request); return false; }
    pthread_mutex_lock(&sRequestMutex);
    unsigned count = 0;
    for (ModRequest *at = sRequestHead; at; at = at->next) ++count;
    if (count >= 64) {
        pthread_mutex_unlock(&sRequestMutex);
        free(request);
        return false;
    }
    if (sRequestTail) sRequestTail->next = request;
    else sRequestHead = request;
    sRequestTail = request;
    pthread_mutex_unlock(&sRequestMutex);
    return true;
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
        } else if (request->type == MOD_REQUEST_MENU_VALUE) {
            Mod *target = find_mod(request->target_name);
            if (target) {
                int index = 0;
                for (Mod *at = sMods; at && at != target; at = at->next) ++index;
                if (index < mod_runtime_mod_count())
                    ok = mod_runtime_menu_set_value(index, request->item_index, request->value);
            }
        } else if (request->type == MOD_REQUEST_PLAYER_DYNOS_MODEL) {
            DynosModel *model = NULL;
            for (DynosModel *at = sDynosModels; at; at = at->next_global) {
                if (!strcmp(at->name, request->path) && at->owner && at->owner->enabled &&
                    at->owner->loaded && gLoadedGraphNodes &&
                    gLoadedGraphNodes[at->native_slot] == at->graph) {
                    model = at;
                    break;
                }
            }
            if (model && gMarioState && gMarioState->marioObj) {
                sPlayerDynosModel = model;
                sPlayerDynosTraversalFrames = 0;
                gMarioState->marioObj->header.gfx.sharedChild = model->graph;
                mod_logf("info", model->owner->name,
                         "selected registered DynOS actor %s for local Mario", model->name);
                ok = true;
            } else {
                mod_log("warning", NULL,
                        "requested local DynOS actor is not active; enable its mod and load its model first");
            }
        } else if (request->type == MOD_REQUEST_PLAYER_NATIVE_MODEL) {
            sPlayerDynosModel = NULL;
            sPlayerDynosTraversalFrames = 0;
            if (gMarioState && gMarioState->marioObj && gLoadedGraphNodes && gLoadedGraphNodes[MODEL_MARIO]) {
                gMarioState->marioObj->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_MARIO];
                ok = true;
            }
            if (ok) mod_log("info", NULL, "restored the native Mario model");
        }
        if (!ok && request->type != MOD_REQUEST_IMPORT_ZIP)
            mod_log("warning", NULL, "queued mod management request could not be applied");
        free(request->path); free(request);
    }
}

#ifdef MOD_RUNTIME_HOST_TEST
void mod_runtime_host_process_requests(void) {
    process_requests();
}
#endif

void mod_runtime_update(void) {
    if (!sModsLoaded) {
        resolve_startup_mod_conflicts();
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

bool mod_runtime_before_phys_step(struct MarioState *m, int step_type, uint32_t step_arg,
                                  int32_t *step_result_override) {
    if (!sLua || !m || !step_result_override) return false;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_BEFORE_PHYS_STEP) continue;
        push_mario(sLua);
        lua_pushinteger(sLua, step_type);
        lua_pushinteger(sLua, step_arg);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_BEFORE_PHYS_STEP", 3, 1)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
            continue;
        }
        if (lua_isnil(sLua, -1)) {
            lua_pop(sLua, 1);
            continue;
        }
        int is_number = 0;
        lua_Integer value = lua_tointegerx(sLua, -1, &is_number);
        lua_pop(sLua, 1);
        if (!is_number || value < INT32_MIN || value > INT32_MAX) {
            if (!hook->invalid_output_reported) {
                mod_logf("warning", hook->owner->name,
                         "HOOK_BEFORE_PHYS_STEP must return an integer step result; ignoring invalid output from %s",
                         hook->file);
                hook->invalid_output_reported = true;
            }
            continue;
        }
        *step_result_override = (int32_t)value;
        return true; /* CoopDX's HOOK_RETURN_ON_OUTPUT_SET stops after the first output. */
    }
    return false;
}

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

void mod_runtime_on_interact(struct MarioState *m, struct Object *obj, uint32_t interact_type, bool interacted) {
    if (!sLua || !m || !obj) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_INTERACT) continue;
        push_mario(sLua); push_object(sLua, obj);
        lua_pushinteger(sLua, (lua_Integer)interact_type);
        lua_pushboolean(sLua, interacted);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ON_INTERACT", 4, 0)) {
            hook->enabled = false; release_ref(&hook->function_ref);
        }
    }
}

bool mod_runtime_allow_interact(struct MarioState *m, struct Object *obj, uint32_t interact_type) {
    bool allow_interact = true;
    if (!sLua || !m || !obj) return allow_interact;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ALLOW_INTERACT) continue;
        push_mario(sLua);
        push_object(sLua, obj);
        lua_pushinteger(sLua, (lua_Integer)interact_type);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ALLOW_INTERACT", 3, 1)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
            continue;
        }
        if (lua_isboolean(sLua, -1)) {
            allow_interact = lua_toboolean(sLua, -1) != 0;
        } else if (!lua_isnil(sLua, -1) && !hook->invalid_output_reported) {
            mod_logf("warning", hook->owner->name,
                     "HOOK_ALLOW_INTERACT must return a boolean or nil; ignoring output from %s",
                     hook->file);
            hook->invalid_output_reported = true;
        }
        lua_pop(sLua, 1);
    }
    return allow_interact;
}

bool mod_runtime_mario_action(struct MarioState *m, int *in_loop) {
    if (!sLua || !m) return false;
    for (ModAction *action = sActions; action; action = action->next_global) {
        if (!action->enabled || !action->owner->enabled || action->id != m->action || action->every_frame_ref == LUA_NOREF) continue;
        push_mario(sLua);
        if (call_ref(action->owner, action->file, action->every_frame_ref, "hook_mario_action.every_frame", 1, 1)) {
            bool result_is_continue = false;
            if (lua_isboolean(sLua, -1)) {
                *in_loop = lua_toboolean(sLua, -1);
            } else if (lua_isnumber(sLua, -1)) {
                int is_integer = 0;
                lua_Integer result = lua_tointegerx(sLua, -1, &is_integer);
                if (!is_integer) {
                    mod_logf("warning", action->owner->name,
                             "hook_mario_action.every_frame returned a non-integer number; treating it as 0");
                    *in_loop = FALSE;
                } else if (result == -1 && !(m->action & ACT_FLAG_CUSTOM_ACTION)) {
                    /* Upstream's legacy action result lets native actions continue. */
                    result_is_continue = true;
                } else if (result < 0) {
                    mod_logf("warning", action->owner->name,
                             "hook_mario_action.every_frame returned unsupported negative result %lld; treating it as 0",
                             (long long)result);
                    *in_loop = FALSE;
                } else {
                    *in_loop = result != 0;
                }
            } else {
                *in_loop = FALSE;
            }
            lua_pop(sLua, 1);
            if (result_is_continue) return false;
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

uint32_t mod_runtime_mario_action_interaction_type(struct MarioState *m) {
    if (!sLua || !m) return 0;
    uint32_t interaction_type = 0;
    for (ModAction *action = sActions; action; action = action->next_global) {
        if (action->enabled && action->owner->enabled && action->id == m->action)
            interaction_type |= action->interaction_type;
    }
    return interaction_type;
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

void mod_runtime_hud_render(void) {
    mod_hud_reset_frame();
    call_hud_event(HOOK_ON_HUD_RENDER);
    render_mod_popups();
}

void mod_runtime_hud_render_behind(void) {
    mod_hud_reset_frame();
    call_hud_event(HOOK_ON_HUD_RENDER_BEHIND);
}

void mod_runtime_object_render(struct Object *object) {
    if (object && gMarioState && object == gMarioState->marioObj && sPlayerDynosModel && sPlayerDynosModel->owner &&
        sPlayerDynosModel->owner->enabled && sPlayerDynosModel->owner->loaded &&
        gLoadedGraphNodes &&
        gLoadedGraphNodes[sPlayerDynosModel->native_slot] == sPlayerDynosModel->graph) {
        object->header.gfx.sharedChild = sPlayerDynosModel->graph;
#if defined(IOS_BUILD)
        uint64_t traversals = ++sPlayerDynosTraversalFrames;
        if (traversals == 1 || traversals % 120 == 0)
            gfx_ios_diag_log("DynOS local actor attached at object render traversal=%llu extendedModel=%u", (unsigned long long)traversals, sPlayerDynosModel->extended_id);
#endif
    }
    if (!sLua || !object || !object_has_render_hook(object)) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_ON_OBJECT_RENDER) continue;
        push_object(sLua, object);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_ON_OBJECT_RENDER", 1, 0)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
}

void mod_runtime_mirror_mario_render(struct GraphNodeObject *mirror_mario, int player_index) {
    if (!sLua || !mirror_mario || player_index < 0 || player_index >= 1) return;
    if (sMirrorMarioGraph != mirror_mario) {
        sMirrorMarioGraph = mirror_mario;
        memcpy(sMirrorMarioShadowPosition, mirror_mario->pos, sizeof(sMirrorMarioShadowPosition));
        sMirrorMarioDisableAutomaticShadowPosition = false;
    } else if (!sMirrorMarioDisableAutomaticShadowPosition) {
        memcpy(sMirrorMarioShadowPosition, mirror_mario->pos, sizeof(sMirrorMarioShadowPosition));
    }

    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_MIRROR_MARIO_RENDER) continue;
        push_graph_node_object(sLua, mirror_mario, sMirrorMarioShadowPosition,
                               &sMirrorMarioDisableAutomaticShadowPosition);
        lua_pushinteger(sLua, player_index);
        if (!call_ref(hook->owner, hook->file, hook->function_ref,
                      "HOOK_MIRROR_MARIO_RENDER", 2, 0)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
}

bool mod_runtime_graph_node_shadow_position(struct GraphNodeObject *graph, float position[3]) {
    if (!graph || !position) return false;
    if (graph == sMirrorMarioGraph && sMirrorMarioDisableAutomaticShadowPosition) {
        memcpy(position, sMirrorMarioShadowPosition, sizeof(sMirrorMarioShadowPosition));
        return true;
    }
    /* Object starts with ObjectNode, whose first field is this graph node. */
    struct Object *object = (struct Object *)graph;
    ObjectReference *ref = object_reference(object, false);
    if (!ref || ref->object->activeFlags == ACTIVE_FLAG_DEACTIVATED ||
        !ref->disable_automatic_shadow_position) return false;
    memcpy(position, ref->shadow_position, sizeof(ref->shadow_position));
    return true;
}

void mod_runtime_object_set_model(struct Object *object, int native_model, int extended_model) {
    if (!sLua || !object) return;
    for (ModHook *hook = sHooks; hook; hook = hook->next_global) {
        if (!hook->enabled || !hook->owner->enabled || hook->event != HOOK_OBJECT_SET_MODEL) continue;
        push_object(sLua, object);
        lua_pushinteger(sLua, native_model);
        lua_pushinteger(sLua, extended_model);
        if (!call_ref(hook->owner, hook->file, hook->function_ref, "HOOK_OBJECT_SET_MODEL", 3, 0)) {
            hook->enabled = false;
            release_ref(&hook->function_ref);
        }
    }
}
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

bool mod_runtime_behavior_call_lua(uint32_t token) {
    if (!sLua || !gCurrentObject || !gCurrentObject->behavior) return false;
    ModBehavior *behavior = NULL;
    for (ModBehavior *at = sBehaviors; at; at = at->next_global) {
        if (at->dynos_behavior && (const BehaviorScript *)at->script == gCurrentObject->behavior) {
            behavior = at;
            break;
        }
    }
    if (!behavior || !behavior->enabled || !behavior->owner->enabled) {
        if (behavior) gCurrentObject->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        return false;
    }
    if (!token || token > behavior->lua_token_count || !behavior->lua_function_refs ||
        behavior->lua_function_refs[token - 1] == LUA_NOREF) {
        mod_logf("error", behavior->owner->name,
                 "DynOS behavior %s received unresolved CALL_NATIVE_EXT token %u from %s",
                 behavior->name, token, behavior->file);
        behavior->enabled = false;
        gCurrentObject->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        return false;
    }
    if (!call_ref(behavior->owner, behavior->file, behavior->lua_function_refs[token - 1],
                  "DynOS CALL_NATIVE_EXT", 0, 0)) {
        release_ref(&behavior->lua_function_refs[token - 1]);
        behavior->enabled = false;
        gCurrentObject->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        return false;
    }
    return true;
}

void mod_runtime_behavior_unsupported_command(unsigned opcode) {
    if (!gCurrentObject) return;
    const char *name = "unknown behavior";
    for (ModBehavior *at = sBehaviors; at; at = at->next_global) {
        if (at->dynos_behavior && (const BehaviorScript *)at->script == gCurrentObject->behavior) {
            name = at->name ? at->name : "unnamed DynOS behavior";
            mod_logf("error", at->owner->name,
                     "DynOS behavior %s reached unsupported command 0x%02x; deactivating its object",
                     name, opcode);
            break;
        }
    }
    if (!strcmp(name, "unknown behavior"))
        mod_logf("error", NULL, "behavior script reached out-of-range command 0x%02x; deactivating its object", opcode);
    gCurrentObject->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

static ModNativeBehaviorState *native_behavior_state(ModBehavior *behavior, struct Object *object,
                                                      uint32_t token, bool create) {
    for (ModNativeBehaviorState *state = sNativeBehaviorStates; state; state = state->next) {
        if (state->behavior == behavior && state->object == object && state->token == token)
            return state;
    }
    if (!create || sNativeBehaviorStateCount >= MOD_MAX_NATIVE_BEHAVIOR_STATES) return NULL;
    ModNativeBehaviorState *state = calloc(1, sizeof(*state));
    if (!state) return NULL;
    state->behavior = behavior;
    state->object = object;
    state->token = token;
    state->next = sNativeBehaviorStates;
    sNativeBehaviorStates = state;
    ++sNativeBehaviorStateCount;
    return state;
}

static void remove_native_behavior_states(Mod *owner, struct Object *object) {
    ModNativeBehaviorState **link = &sNativeBehaviorStates;
    while (*link) {
        ModNativeBehaviorState *state = *link;
        bool matches_owner = !owner || state->behavior->owner == owner;
        bool matches_object = !object || state->object == object;
        if (matches_owner && matches_object) {
            *link = state->next;
            free(state);
            if (sNativeBehaviorStateCount) --sNativeBehaviorStateCount;
        } else {
            link = &state->next;
        }
    }
}

bool mod_runtime_native_behavior_tick(void) {
    if (!sLua || !gCurrentObject || gCurrentObject->activeFlags == ACTIVE_FLAG_DEACTIVATED)
        return false;
    lua_Integer behavior_id = native_behavior_id_from_script(gCurrentObject->behavior);
    if (behavior_id < 0 || behavior_id >= MOD_NATIVE_BEHAVIOR_COUNT) return false;
    ObjectReference *reference = object_reference(gCurrentObject, true);
    if (!reference) return false;

    bool replace_native_behavior = false;
    for (ModBehavior *hook = sBehaviors; hook; hook = hook->next_global) {
        if (!hook->native_hook || !hook->enabled || !hook->owner->enabled ||
            hook->id != (uint32_t)behavior_id) continue;
        ModNativeBehaviorState *state = native_behavior_state(hook, gCurrentObject, reference->token, true);
        if (!state) {
            if (!hook->state_warned) {
                hook->state_warned = true;
                mod_logf("warning", hook->owner->name,
                         "native behavior hook state limit (%u) reached; preserving original behavior",
                         MOD_MAX_NATIVE_BEHAVIOR_STATES);
            }
            continue;
        }

        bool callback_ok = true;
        if (!state->initialized) {
            if (hook->init_ref != LUA_NOREF) {
                push_object(sLua, gCurrentObject);
                callback_ok = call_ref(hook->owner, hook->file, hook->init_ref,
                                       "hook_behavior.init", 1, 0);
            }
            if (callback_ok) state->initialized = true;
        }
        if (callback_ok && hook->loop_ref != LUA_NOREF) {
            push_object(sLua, gCurrentObject);
            callback_ok = call_ref(hook->owner, hook->file, hook->loop_ref,
                                   "hook_behavior.loop", 1, 0);
        }
        if (!callback_ok) {
            hook->enabled = false;
            release_ref(&hook->init_ref);
            release_ref(&hook->loop_ref);
            continue;
        }
        if (hook->replace_behavior) replace_native_behavior = true;
    }
    return replace_native_behavior;
}

void mod_runtime_object_unload(struct Object *obj) {
    if (!obj) return;
    remove_native_behavior_states(NULL, obj);
    remove_render_hook_owners(NULL, obj);
    ObjectReference *ref = object_reference(obj, false);
    if (ref) *ref = (ObjectReference){0};
    ModObject **link = &sObjects;
    while (*link) {
        ModObject *entry = *link;
        if (entry->object == obj) {
            *link = entry->next;
            free(entry);
        } else link = &entry->next;
    }
}

void mod_runtime_clear_objects(void) {
    remove_native_behavior_states(NULL, NULL);
    remove_render_hook_owners(NULL, NULL);
    memset(sObjectRefs, 0, sizeof(sObjectRefs));
    while (sObjects) {
        ModObject *next = sObjects->next;
        free(sObjects);
        sObjects = next;
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
    static char result[MOD_MAX_DISPLAY_NAME];
    result[0] = 0;
    if (index < 0) return NULL;
    pthread_mutex_lock(&sModListMutex);
    for (Mod *m = sMods; m; m = m->next) if (!index--) { snprintf(result, sizeof(result), "%s", m->display_name[0] ? m->display_name : m->name); break; }
    pthread_mutex_unlock(&sModListMutex);
    return result[0] ? result : NULL;
}
static const char *mod_runtime_metadata_string(int index, unsigned field) {
    static char result[4][MOD_MAX_DESCRIPTION];
    if (field >= 4) return NULL;
    result[field][0] = 0;
    if (index < 0) return NULL;
    pthread_mutex_lock(&sModListMutex);
    Mod *mod = mod_runtime_find_index(index);
    if (mod) {
        const char *value = field == 0 ? mod->name : field == 1 ? mod->description :
                            field == 2 ? mod->category : mod->incompatible;
        snprintf(result[field], sizeof(result[field]), "%s", value ? value : "");
    }
    pthread_mutex_unlock(&sModListMutex);
    return result[field][0] ? result[field] : NULL;
}
const char *mod_runtime_mod_package_id(int index) { return mod_runtime_metadata_string(index, 0); }
const char *mod_runtime_mod_description(int index) { return mod_runtime_metadata_string(index, 1); }
const char *mod_runtime_mod_category(int index) { return mod_runtime_metadata_string(index, 2); }
const char *mod_runtime_mod_incompatibilities(int index) { return mod_runtime_metadata_string(index, 3); }
const char *mod_runtime_mod_enabled_conflict(int index) {
    static char result[MOD_MAX_NAME];
    result[0] = 0;
    if (index < 0) return NULL;
    pthread_mutex_lock(&sModListMutex);
    Mod *mod = mod_runtime_find_index(index);
    Mod *conflict = mod ? find_enabled_conflict(mod) : NULL;
    if (conflict) snprintf(result, sizeof(result), "%s", conflict->display_name[0] ? conflict->display_name : conflict->name);
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

static Mod *mod_runtime_find_index(int index) {
    if (index < 0) return NULL;
    for (Mod *mod = sMods; mod; mod = mod->next) if (!index--) return mod;
    return NULL;
}

int mod_runtime_menu_item_count(int mod_index) {
    pthread_mutex_lock(&sModListMutex);
    Mod *mod = mod_runtime_find_index(mod_index);
    int count = mod && mod->loaded && mod->enabled ? (int)mod->menu_item_count : 0;
    pthread_mutex_unlock(&sModListMutex);
    return count;
}

bool mod_runtime_menu_get_item(int mod_index, int item_index, ModRuntimeMenuItem *out) {
    if (!out || item_index < 0) return false;
    pthread_mutex_lock(&sModListMutex);
    Mod *mod = mod_runtime_find_index(mod_index);
    bool valid = mod && mod->loaded && mod->enabled && (size_t)item_index < mod->menu_item_count;
    if (valid) {
        const ModMenuItem *item = &mod->menu_items[item_index];
        out->type = item->type;
        snprintf(out->label, sizeof(out->label), "%s", item->label);
        out->value = item->value;
        out->minimum = item->minimum;
        out->maximum = item->maximum;
    }
    pthread_mutex_unlock(&sModListMutex);
    return valid;
}

bool mod_runtime_menu_set_value(int mod_index, int item_index, int64_t value) {
    if (!sLua || item_index < 0) return false;
    char file[SYS_MAX_PATH];
    ModMenuItem snapshot;
    pthread_mutex_lock(&sModListMutex);
    Mod *mod = mod_runtime_find_index(mod_index);
    if (!mod || !mod->enabled || !mod->loaded || (size_t)item_index >= mod->menu_item_count) {
        pthread_mutex_unlock(&sModListMutex);
        return false;
    }
    ModMenuItem *item = &mod->menu_items[item_index];
    if (item->type == MOD_MENU_BUTTON) {
        if (value != 0) { pthread_mutex_unlock(&sModListMutex); return false; }
    } else if (item->type == MOD_MENU_CHECKBOX) {
        if (value != 0 && value != 1) { pthread_mutex_unlock(&sModListMutex); return false; }
        item->value = value;
    } else {
        if (value < item->minimum || value > item->maximum) { pthread_mutex_unlock(&sModListMutex); return false; }
        item->value = value;
    }
    snapshot = *item;
    snprintf(file, sizeof(file), "%s", item->file ? item->file : "main.lua");
    pthread_mutex_unlock(&sModListMutex);
    lua_pushinteger(sLua, item_index);
    int nargs = 1;
    if (snapshot.type != MOD_MENU_BUTTON) {
        if (snapshot.type == MOD_MENU_CHECKBOX) lua_pushboolean(sLua, value != 0);
        else lua_pushinteger(sLua, (lua_Integer)value);
        ++nargs;
    }
    if (call_ref(mod, file, snapshot.callback_ref, "mod menu callback", nargs, 0)) return true;
    pthread_mutex_lock(&sModListMutex);
    if ((size_t)item_index < mod->menu_item_count && mod->menu_items[item_index].callback_ref == snapshot.callback_ref)
        release_ref(&mod->menu_items[item_index].callback_ref);
    pthread_mutex_unlock(&sModListMutex);
    return false;
}

bool mod_runtime_dispatch_chat_command(const char *name, const char *message) {
    if (!sLua || !name) return false;
    for (Mod *mod = sMods; mod; mod = mod->next) {
        if (!mod->enabled || !mod->loaded) continue;
        for (ModCommand *command = mod->commands; command; command = command->next) {
            if (strcasecmp(command->name, name)) continue;
            if (command->callback_ref == LUA_NOREF) continue;
            lua_pushstring(sLua, message ? message : "");
            if (!call_ref(mod, command->file, command->callback_ref, "hook_chat_command", 1, 1)) {
                release_ref(&command->callback_ref);
                continue;
            }
            bool handled = lua_toboolean(sLua, -1);
            lua_pop(sLua, 1);
            if (handled) return true;
        }
    }
    return false;
}

bool mod_runtime_set_enabled(int index, bool enabled) {
    if (index < 0) return false;
    for (Mod *mod = sMods; mod; mod = mod->next) if (!index--) {
        if (enabled) {
            pthread_mutex_lock(&sModListMutex);
            Mod *conflict = find_enabled_conflict(mod);
            pthread_mutex_unlock(&sModListMutex);
            if (conflict) {
                mod_logf("warning", mod->name, "cannot enable: incompatible with active mod '%s'", conflict->name);
                return false;
            }
        }
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
            "DynOS pack imported; supported actor models, PNG-backed textures, and a limited behavior subset can activate, while animation playback, collision assets, and some GeoLayout or material features remain unavailable");
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
                     "%u model bins · %u texture assets · %u collision · %u levels · %u behaviors · %u audio · %u other · support varies",
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
        mod_storage_free_entries(mod);
        free(mod); mod = next;
    }
    sMods = NULL; sHooks = NULL; sActions = NULL; sBehaviors = NULL;
    sNextBehaviorId = 1; sNextObjectToken = 1; sStarExitSettingOrder = 0; sLevelSettingOrder = 0;
    sMirrorMarioGraph = NULL;
    memset(sMirrorMarioShadowPosition, 0, sizeof(sMirrorMarioShadowPosition));
    sMirrorMarioDisableAutomaticShadowPosition = false;
    while (sDynosPacks) { DynosPack *next = sDynosPacks->next; free(sDynosPacks); sDynosPacks = next; }
    mod_runtime_clear_objects();
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
