#ifndef SM64IOS_MOD_RUNTIME_H
#define SM64IOS_MOD_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct MarioState;
struct Object;

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
/* Returns true when a Lua hook supplies the physics step result override. */
bool mod_runtime_before_phys_step(struct MarioState *m, int step_type, uint32_t step_arg,
                                  int32_t *step_result_override);
void mod_runtime_before_set_action(struct MarioState *m, uint32_t *action, uint32_t action_arg, bool *cancel);
void mod_runtime_after_set_action(struct MarioState *m);
void mod_runtime_on_interact(struct MarioState *m, struct Object *obj, uint32_t interact_type, bool interacted);
bool mod_runtime_allow_interact(struct MarioState *m, struct Object *obj, uint32_t interact_type);
bool mod_runtime_mario_action(struct MarioState *m, int *in_loop);
bool mod_runtime_mario_gravity(struct MarioState *m);
void mod_runtime_hud_render(void);
void mod_runtime_level_init(int type, int level, int area, int node, int arg);
void mod_runtime_object_unload(struct Object *obj);
void mod_runtime_clear_objects(void);

/* Called by the Lua-backed behavior bytecode and exposed to Lua mods. */
void mod_runtime_behavior_tick(void);
const uintptr_t *mod_runtime_behavior_for_list(int list);

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
