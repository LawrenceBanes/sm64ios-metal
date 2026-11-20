#include "pc/mods/mod_runtime.h"

bool mod_runtime_stay_in_level_after_star(void) { return false; }

int32_t mod_runtime_level_value(ModRuntimeLevelValue value) {
    if (value == MOD_LEVEL_VALUE_FIX_VANISH_FLOORS) return 1;
    if (value == MOD_LEVEL_VALUE_CELL_HEIGHT_LIMIT) return 20000;
    return 0;
}

bool mod_runtime_mario_gravity(struct MarioState *m) {
    (void)m;
    return false;
}

bool mod_runtime_before_phys_step(struct MarioState *m, int step_type, uint32_t step_arg,
                                  int32_t *step_result_override) {
    (void)m;
    (void)step_type;
    (void)step_arg;
    (void)step_result_override;
    return false;
}

bool mod_runtime_allow_interact(struct MarioState *m, struct Object *obj, uint32_t interact_type) {
    (void)m;
    (void)obj;
    (void)interact_type;
    return true;
}

bool mod_runtime_allow_hazard_surface(struct MarioState *m, int hazard_type) {
    (void)m;
    (void)hazard_type;
    return true;
}

void mod_runtime_on_warp(uint8_t warp_type, int16_t level, uint8_t area, uint8_t node, uint32_t arg) {
    (void)warp_type;
    (void)level;
    (void)area;
    (void)node;
    (void)arg;
}
