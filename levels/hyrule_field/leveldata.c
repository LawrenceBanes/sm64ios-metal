#include <ultra64.h>
#include "types.h"
#include "surface_terrains.h"
#include "levels/hyrule_field/header.h"

#if defined(IOS_HYRULE_WORLD)
#include "build/hyrule_field_generated.inc.c"
#else
const Gfx hyrule_field_opaque_dl[] = { gsSPEndDisplayList() };
const Gfx hyrule_field_translucent_dl[] = { gsSPEndDisplayList() };
const Collision hyrule_field_collision[] = { COL_INIT(), COL_END() };
#define HYRULE_FIELD_SPAWN_X 0
#define HYRULE_FIELD_SPAWN_Y 0
#define HYRULE_FIELD_SPAWN_Z 0
#define HYRULE_FIELD_SPAWN_YAW ((s16)0)
#endif
