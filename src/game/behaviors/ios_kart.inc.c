#include "game/mario.h"
#include "game/object_helpers.h"

void bhv_ios_kart_init(void) {
    o->oAction = 0;
    obj_scale(o, 1.0f);
    mario_ios_register_kart(o);
}

void bhv_ios_kart_loop(void) {
    mario_ios_sync_kart_object(o);
    o->oInteractStatus = 0;
}
