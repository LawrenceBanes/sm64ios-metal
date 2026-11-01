#include <ultra64.h>
#include "sm64.h"
#include "surface_terrains.h"
#include "geo_commands.h"

#include "make_const_nonconst.h"

// Note: This bin does not use bin IDs, unlike the other segmented bins.
#include "mario/model.inc.c"
#include "portal/model.inc.c"
#include "ios_kart/model.inc.c"

// Temporary Luigi look: reuse Mario's geometry and animations while changing
// the shared red shirt/cap lighting to green. This is not a separate Luigi model.
void mario_set_temporary_luigi_palette(s32 enabled) {
    u8 ambientR = enabled ? 0x00 : 0x7f;
    u8 ambientG = enabled ? 0x7f : 0x00;
    u8 lightR = enabled ? 0x00 : 0xff;
    u8 lightG = enabled ? 0xff : 0x00;
    mario_red_lights_group.a.l.col[0] = ambientR;
    mario_red_lights_group.a.l.col[1] = ambientG;
    mario_red_lights_group.a.l.col[2] = 0x00;
    mario_red_lights_group.a.l.colc[0] = ambientR;
    mario_red_lights_group.a.l.colc[1] = ambientG;
    mario_red_lights_group.a.l.colc[2] = 0x00;
    mario_red_lights_group.l[0].l.col[0] = lightR;
    mario_red_lights_group.l[0].l.col[1] = lightG;
    mario_red_lights_group.l[0].l.col[2] = 0x00;
    mario_red_lights_group.l[0].l.colc[0] = lightR;
    mario_red_lights_group.l[0].l.colc[1] = lightG;
    mario_red_lights_group.l[0].l.colc[2] = 0x00;
}

#include "bubble/model.inc.c"

#include "walk_smoke/model.inc.c"

#include "burn_smoke/model.inc.c"

#include "stomp_smoke/model.inc.c"

#include "water_wave/model.inc.c"

#include "sparkle/model.inc.c"

#include "water_splash/model.inc.c"

#include "white_particle_small/model.inc.c"

#include "sparkle_animation/model.inc.c"
