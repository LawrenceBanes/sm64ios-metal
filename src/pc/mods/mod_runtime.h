#ifndef SM64IOS_MOD_RUNTIME_H
#define SM64IOS_MOD_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOD_LEVEL_VALUE_FIX_VANISH_FLOORS,
    MOD_LEVEL_VALUE_STAR_HEAL,
    MOD_LEVEL_VALUE_MUSHROOM_1UP_HEAL,
    MOD_LEVEL_VALUE_PREVIEW_BLUE_COINS,
    MOD_LEVEL_VALUE_RESPAWN_BLUE_COINS_SWITCH,
    MOD_LEVEL_VALUE_VISIBLE_SECRETS,
    MOD_LEVEL_VALUE_SHOW_STAR_NUMBER,
    MOD_LEVEL_VALUE_EXTENDED_PAUSE_DISPLAY,
    MOD_LEVEL_VALUE_PAUSE_EXIT_ANYWHERE,
    MOD_LEVEL_VALUE_CELL_HEIGHT_LIMIT,
    MOD_LEVEL_VALUE_COINS_REQUIRED_FOR_COIN_STAR,
    MOD_LEVEL_VALUE_COUNT
} ModRuntimeLevelValue;
#include <stddef.h>

struct MarioState;
struct Object;
struct Camera;
struct GraphNodeObject;

typedef struct ModRuntimeNativeTexture {
    const char *asset_path;
    uint32_t width, height;
    int format, size;
} ModRuntimeNativeTexture;

enum {
    MOD_RUNTIME_STEP_TYPE_GROUND = 1,
    MOD_RUNTIME_STEP_TYPE_AIR = 2,
    MOD_RUNTIME_STEP_TYPE_WATER = 3,
    MOD_RUNTIME_STEP_TYPE_HANG = 4,
};

bool mod_runtime_init(void);
void mod_runtime_shutdown(void);
void mod_runtime_update(void);
void mod_runtime_before_mario_update(struct MarioState *m);
void mod_runtime_after_mario_update(struct MarioState *m);
/* Returns false when a Lua HOOK_ON_DEATH callback vetoes the death warp. */
bool mod_runtime_on_death(struct MarioState *m);
/* Returns false when a Lua HOOK_ON_PAUSE_EXIT callback vetoes the pause-menu exit. */
bool mod_runtime_on_pause_exit(bool used_exit_to_castle);
/* Returns false when HOOK_ON_SET_CAMERA_MODE vetoes the engine camera change. */
bool mod_runtime_on_set_camera_mode(struct Camera *camera, int16_t mode, int16_t frames);
/* Returns false when HOOK_ON_CHANGE_CAMERA_ANGLE vetoes a Lakitu/Mario angle change. */
bool mod_runtime_allow_camera_angle(int32_t angle);
/* Returns false when a Lua HOOK_ALLOW_HAZARD_SURFACE callback vetoes a hazard. */
bool mod_runtime_allow_hazard_surface(struct MarioState *m, int hazard_type);
void mod_runtime_on_warp(uint8_t warp_type, int16_t level, uint8_t area, uint8_t node, uint32_t arg);
/* Returns true when a HOOK_USE_ACT_SELECT callback supplied a boolean result. */
bool mod_runtime_use_act_select(int32_t level, bool *use_act_select);
/* Returns true when a Lua hook supplies the physics step result override. */
bool mod_runtime_before_phys_step(struct MarioState *m, int step_type, uint32_t step_arg,
                                  int32_t *step_result_override);
void mod_runtime_before_set_action(struct MarioState *m, uint32_t *action, uint32_t action_arg, bool *cancel);
void mod_runtime_after_set_action(struct MarioState *m);
void mod_runtime_on_interact(struct MarioState *m, struct Object *obj, uint32_t interact_type, bool interacted);
bool mod_runtime_stay_in_level_after_star(void);
int32_t mod_runtime_level_value(ModRuntimeLevelValue value);
bool mod_runtime_allow_interact(struct MarioState *m, struct Object *obj, uint32_t interact_type);
bool mod_runtime_mario_action(struct MarioState *m, int *in_loop);
uint32_t mod_runtime_mario_action_interaction_type(struct MarioState *m);
bool mod_runtime_mario_gravity(struct MarioState *m);
void mod_runtime_hud_render_behind(void);
void mod_runtime_hud_render(void);
bool mod_runtime_hud_hidden(void);
void mod_runtime_object_render(struct Object *object);
void mod_runtime_mirror_mario_render(struct GraphNodeObject *mirror_mario, int player_index);
bool mod_runtime_graph_node_shadow_position(struct GraphNodeObject *graph, float position[3]);
void mod_runtime_object_set_model(struct Object *object, int native_model, int extended_model);
int mod_runtime_extended_model_id(int native_model);
void mod_runtime_level_init(int type, int level, int area, int node, int arg);
void mod_runtime_object_unload(struct Object *obj);
void mod_runtime_clear_objects(void);
/* Returns a borrowed RGBA8 DynOS texture while its owning mod is active. */
bool mod_runtime_dynamic_texture_lookup(const char *key, const uint8_t **rgba,
                                        uint32_t *width, uint32_t *height);
/* Resolves native texture symbols against assets bundled by EXTERNAL_DATA builds. */
bool mod_runtime_native_texture_lookup(const char *name, ModRuntimeNativeTexture *texture);

/* Called by the Lua-backed behavior bytecode and exposed to Lua mods. */
void mod_runtime_behavior_tick(void);
/* Executes a Lua token referenced by DynOS CALL_NATIVE_EXT (0x3C). */
bool mod_runtime_behavior_call_lua(uint32_t token);
/* Prevents malformed or unsupported behavior bytecode from indexing beyond
 * the native command table and deactivates the affected object. */
void mod_runtime_behavior_unsupported_command(unsigned opcode);
/* Runs per-object callbacks registered by hook_behavior for native behaviors.
 * Return true to skip the native behavior script for this frame. */
bool mod_runtime_native_behavior_tick(void);
const uintptr_t *mod_runtime_behavior_for_list(int list);
#ifdef MOD_RUNTIME_HOST_TEST
bool mod_runtime_host_load_dynos_model(size_t mod_index, const char *name,
                                       int *extended_id, char *error,
                                       size_t error_size);
bool mod_runtime_host_set_object_model(struct Object *object, int extended_id,
                                      char *error, size_t error_size);
#endif

/* ZIP importing uses this after a platform document picker has copied the ZIP
 * into app-owned temporary storage. Returns an owned, installed mod directory. */
bool mod_runtime_import_zip(const char *zip_path, char *installed_path, unsigned installed_path_size,
                            char *error, unsigned error_size);

int mod_runtime_mod_count(void);
const char *mod_runtime_mod_name(int index);
bool mod_runtime_mod_enabled(int index);
enum { MOD_RUNTIME_MENU_BUTTON = 0, MOD_RUNTIME_MENU_CHECKBOX = 1, MOD_RUNTIME_MENU_SLIDER = 2 };
typedef struct {
    int type;
    char label[256];
    int64_t value;
    int64_t minimum;
    int64_t maximum;
} ModRuntimeMenuItem;
int mod_runtime_menu_item_count(int mod_index);
bool mod_runtime_menu_get_item(int mod_index, int item_index, ModRuntimeMenuItem *item);
/* Invoke from the game thread. Checkboxes accept only 0/1; sliders must be in range. */
bool mod_runtime_menu_set_value(int mod_index, int item_index, int64_t value);
bool mod_runtime_request_menu_set_value(int mod_index, int item_index, int64_t value);
/* Called by the native chat/input layer on the game thread with the token and remainder. */
bool mod_runtime_dispatch_chat_command(const char *name, const char *message);
bool mod_runtime_set_enabled(int index, bool enabled);
bool mod_runtime_reload(int index);
bool mod_runtime_request_enabled(int index, bool enabled);
bool mod_runtime_request_reload(int index);
bool mod_runtime_request_import_zip(const char *zip_path);
int mod_runtime_dynos_pack_count(void);
const char *mod_runtime_dynos_pack_name(int index);
const char *mod_runtime_dynos_pack_summary(int index);

#endif
