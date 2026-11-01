#include "game/mario.h"
#include "game/object_helpers.h"

void bhv_portal_blue_init(void) {
    cur_obj_hide();
}

void bhv_portal_blue_loop(void) {
    mario_ios_sync_portal_object(o, MARIO_IOS_PORTAL_BLUE);
}

void bhv_portal_orange_init(void) {
    cur_obj_hide();
}

void bhv_portal_orange_loop(void) {
    mario_ios_sync_portal_object(o, MARIO_IOS_PORTAL_ORANGE);
}
