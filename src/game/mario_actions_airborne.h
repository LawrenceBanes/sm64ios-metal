#ifndef MARIO_ACTIONS_AIRBORNE_H
#define MARIO_ACTIONS_AIRBORNE_H

#include <PR/ultratypes.h>

#include "types.h"

s32 mario_execute_airborne_action(struct MarioState *m);
void play_flip_sounds(struct MarioState *m, s16 frame1, s16 frame2, s16 frame3);

#endif // MARIO_ACTIONS_AIRBORNE_H
