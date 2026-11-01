#include "game/mario.h"
#include "game/object_helpers.h"

void bhv_ios_kart_init(void) {
    o->oAction = 0;
    o->oAnimState = 0;
    obj_scale(o, 1.0f);
    mario_ios_register_kart(o);
}

void bhv_ios_kart_loop(void) {
    mario_ios_sync_kart_object(o);
    o->oInteractStatus = 0;
}

Gfx *geo_ios_kart_wheel_rotation(s32 callContext, struct GraphNode *node, UNUSED Mat4 *mtx) {
    if (callContext == GEO_CONTEXT_RENDER) {
        struct GraphNodeGenerated *generated = (struct GraphNodeGenerated *)node;
        struct GraphNodeRotation *rotation = (struct GraphNodeRotation *)node->next;
        struct Object *kart = (struct Object *)gCurGraphNodeObject;
        rotation->rotation[0] = (s16)kart->oAnimState;
        rotation->rotation[1] = generated->parameter < 2 ? (s16)kart->oAngleVelYaw : 0;
        rotation->rotation[2] = 0;
    }
    return NULL;
}
