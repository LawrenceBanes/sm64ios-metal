#include <assert.h>
#include <stdio.h>

#include "../src/pc/controller/controller_dpad.h"

int main(void) {
    assert(controller_sdl_dpad_mask(false, false, false, false) == 0);
    assert(controller_sdl_dpad_mask(true, false, false, false) == U_JPAD);
    assert(controller_sdl_dpad_mask(false, true, false, false) == D_JPAD);
    assert(controller_sdl_dpad_mask(false, false, true, false) == L_JPAD);
    assert(controller_sdl_dpad_mask(false, false, false, true) == R_JPAD);
    assert(controller_sdl_dpad_mask(true, true, true, true) ==
           (U_JPAD | D_JPAD | L_JPAD | R_JPAD));
    puts("SDL controller d-pad maps to N64 d-pad masks");
    return 0;
}
