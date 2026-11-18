#ifndef SM64IOS_MOD_RUNTIME_H
#define SM64IOS_MOD_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

struct MarioState;
struct Object;

bool mod_runtime_init(void);
void mod_runtime_shutdown(void);
void mod_runtime_update(void);
void mod_runtime_before_mario_update(struct MarioState *m);
void mod_runtime_after_mario_update(struct MarioState *m);
void mod_runtime_before_set_action(struct MarioState *m, uint32_t *action, uint32_t action_arg, bool *cancel);
void mod_runtime_after_set_action(struct MarioState *m);
bool mod_runtime_mario_action(struct MarioState *m, int *in_loop);
bool mod_runtime_mario_gravity(struct MarioState *m);
void mod_runtime_hud_render(void);
void mod_runtime_level_init(int type, int level, int area, int node, int arg);
void mod_runtime_object_unload(struct Object *obj);

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
bool mod_runtime_set_enabled(int index, bool enabled);
bool mod_runtime_reload(int index);
bool mod_runtime_request_enabled(int index, bool enabled);
bool mod_runtime_request_reload(int index);
bool mod_runtime_request_import_zip(const char *zip_path);
int mod_runtime_dynos_pack_count(void);
const char *mod_runtime_dynos_pack_name(int index);
const char *mod_runtime_dynos_pack_summary(int index);

#endif
