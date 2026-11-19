#include "pc/mods/mod_runtime.h"

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
