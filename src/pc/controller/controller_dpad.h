#ifndef CONTROLLER_DPAD_H
#define CONTROLLER_DPAD_H

#include <stdbool.h>
#include <PR/os_cont.h>

/* Shared mapping for SDL GameController d-pad buttons and host regression tests. */
static inline unsigned int controller_sdl_dpad_mask(bool up, bool down, bool left, bool right) {
    unsigned int mask = 0;
    if (up) mask |= U_JPAD;
    if (down) mask |= D_JPAD;
    if (left) mask |= L_JPAD;
    if (right) mask |= R_JPAD;
    return mask;
}

#endif
