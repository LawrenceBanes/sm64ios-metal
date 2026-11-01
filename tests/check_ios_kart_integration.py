#!/usr/bin/env python3
"""Source-level regression checks for the iOS kart mount/drive loop."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text()


sm64 = read("include/sm64.h")
mario = read("src/game/mario.c")
moving = read("src/game/mario_actions_moving.c")
airborne = read("src/game/mario_actions_airborne.c")
mario_step = read("src/game/mario_step.c")
behavior = read("data/behavior_data.c")
behavior_code = read("src/game/behaviors/ios_kart.inc.c")
level = read("levels/castle_grounds/script.c")
models = read("levels/scripts.c")
group0 = read("actors/group0.c")
geo = read("actors/ios_kart/geo.inc.c")
ui = read("src/pc/gfx/gfx_uikit.m")
physics = read("src/game/ios_kart.c")

ground = re.search(r"#define ACT_IOS_KART_GROUND\s+(0x[0-9A-Fa-f]+)", sm64)
air = re.search(r"#define ACT_IOS_KART_AIR\s+(0x[0-9A-Fa-f]+)", sm64)
assert ground and air
assert int(ground.group(1), 16) & 0x1C0 == 0x40
assert int(air.group(1), 16) & 0x1C0 == 0x80
assert "case ACT_IOS_KART_GROUND:" in moving
assert "case ACT_IOS_KART_AIR:" in airborne
assert "perform_ground_step(m)" in moving[moving.index("s32 act_ios_kart_ground"):]
assert "perform_air_step(m, 0)" in airborne[airborne.index("s32 act_ios_kart_air"):]

# Controls, mount/dismount, object spawning, actor lifetime and rescue behavior.
assert "buttonPressed & L_TRIG" in mario
assert "buttonDown & B_BUTTON" in mario and "buttonDown & Z_TRIG" in mario
assert "buttonDown & A_BUTTON" in mario
assert "mario_ios_try_mount_kart" in mario and "mario_ios_dismount_kart" in mario
assert "ios_kart_can_mount" in mario and "ios_kart_can_dismount" in mario
assert "#define IOS_KART_MOUNT_DISTANCE 130.0f" in mario
assert "mario_ios_get_kart_mount_position" in mario
assert "m->pos[0] = mountPosition[0]" in mario
assert "spawn_object(m->marioObj, MODEL_IOS_KART, bhvIOSKart)" in mario
assert "mario_ios_get_kart_spawn_position" in mario and "floor->normal.y < 0.5f" in mario
assert "mario_ios_find_driver_exit_position" in mario
assert "No safe space beside the kart to exit" in mario
assert "mario_ios_detach_interrupted_kart" in mario
assert "mario_ios_restore_kart_to_last_ground" in mario
assert "mario_ios_kart_wall_collision" in moving and "mario_ios_kart_wall_collision" in airborne
assert "mario_step_is_ios_kart" in mario_step
assert "isKart ? 74.0f : 50.0f" in mario_step
assert "clearanceHeight = isKart ? 220.0f : 160.0f" in mario_step
assert "mario_ios_sync_kart_object" in behavior_code
assert "DROP_TO_FLOOR()" in behavior[behavior.index("const BehaviorScript bhvIOSKart[]"):]
assert "MODEL_IOS_KART" in level and "bhvIOSKart" in level
assert "LOAD_MODEL_FROM_GEO(MODEL_IOS_KART" in models
assert "ios_kart/model.inc.c" in group0 and "ios_kart_geo" in geo
assert "mario_ios_request_kart_spawn();" in ui

# Reusable controller remains independent of SM64 and graphics state.
assert "ios_kart_update" in physics
assert "ios_kart_can_mount" in physics and "ios_kart_can_dismount" in physics
assert "ios_kart_velocity" in physics
assert "ios_kart_wall_collision" in physics
assert "IOS_KART_DRIFT_CHARGE" in physics and "IOS_KART_BOOST_DURATION" in physics
reference = read("ios/MK64_KART_REFERENCE.md")
assert "58cfcb022e10f83bc3b889d7e97508cae6837098" in reference
assert "copies no MK64 source" in reference

print("iOS kart action, collision, spawn, mount/dismount, and actor wiring checks passed")
