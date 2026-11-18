#ifndef MARIO_SUPERSLIDE_H
#define MARIO_SUPERSLIDE_H

#include <PR/ultratypes.h>
struct MarioState;
void mario_superslide_reset(void);
void mario_superslide_update(struct MarioState *m);
s32 mario_act_superslide(struct MarioState *m);
#endif
