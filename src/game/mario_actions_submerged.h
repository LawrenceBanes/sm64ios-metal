#ifndef MARIO_ACTIONS_SUBMERGED_H
#define MARIO_ACTIONS_SUBMERGED_H

#include <PR/ultratypes.h>

#include "types.h"

s32 mario_execute_submerged_action(struct MarioState *m);
u32 perform_water_step(struct MarioState *m);

#endif // MARIO_ACTIONS_SUBMERGED_H
