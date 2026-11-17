#ifndef MARIO_HAT_H
#define MARIO_HAT_H

#ifdef IOS_HAT_MOVEMENT

#include <stdbool.h>

struct MarioState;

void mario_hat_reset(void);
void mario_hat_update(struct MarioState *m);
void mario_hat_execute_throw(struct MarioState *m);
void mario_hat_finish_frame(struct MarioState *m);
bool mario_hat_is_away(void);
bool mario_hat_try_bounce(struct MarioState *m, const float previous[3]);

#endif
#endif
