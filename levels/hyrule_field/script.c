#include <ultra64.h>
#include "sm64.h"
#include "behavior_data.h"
#include "model_ids.h"
#include "seq_ids.h"
#include "level_commands.h"
#include "surface_terrains.h"

#include "game/level_update.h"
#include "levels/scripts.h"
#include "make_const_nonconst.h"
#include "levels/hyrule_field/header.h"

#if defined(IOS_HYRULE_WORLD)
#include "build/hyrule_field_spawn.h"
#else
#define HYRULE_FIELD_SPAWN_X 0
#define HYRULE_FIELD_SPAWN_Y 0
#define HYRULE_FIELD_SPAWN_Z 0
#define HYRULE_FIELD_SPAWN_YAW ((s16)0)
#endif

const LevelScript level_hyrule_field_entry[] = {
    INIT_LEVEL(),
    ALLOC_LEVEL_POOL(),
    MARIO(/*model*/ MODEL_MARIO, /*behavior params*/ 0x00000001, /*behavior*/ bhvMario),
    JUMP_LINK(script_func_global_1),
    JUMP_LINK(script_func_global_4),

    AREA(/*index*/ 1, hyrule_field_geo),
        TERRAIN(hyrule_field_collision),
        TERRAIN_TYPE(TERRAIN_GRASS),
    END_AREA(),

    FREE_LEVEL_POOL(),
    MARIO_POS(/*area*/ 1, /*yaw*/ HYRULE_FIELD_SPAWN_YAW,
              /*position*/ HYRULE_FIELD_SPAWN_X, HYRULE_FIELD_SPAWN_Y, HYRULE_FIELD_SPAWN_Z),
    SET_BACKGROUND_MUSIC(/*settingsPreset*/ 0x0000, /*seq*/ SEQ_LEVEL_GRASS),
    CALL(/*arg*/ 0, /*func*/ lvl_init_or_update),
    CALL_LOOP(/*arg*/ 1, /*func*/ lvl_init_or_update),
    CLEAR_LEVEL(),
    SLEEP_BEFORE_EXIT(/*frames*/ 1),
    EXIT(),
};
