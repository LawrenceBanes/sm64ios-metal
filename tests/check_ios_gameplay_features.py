#!/usr/bin/env python3
"""Source-level guards for the opt-in iOS Luigi/Yoshi gameplay prototype."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text()


sm64 = read("include/sm64.h")
mario = read("src/game/mario.c")
moving = read("src/game/mario_actions_moving.c")
airborne = read("src/game/mario_actions_airborne.c")
yoshi = read("src/game/behaviors/yoshi.inc.c")
behavior_data = read("data/behavior_data.c")
level = read("levels/castle_grounds/script.c")
ui = read("src/pc/gfx/gfx_uikit.m")
actor = read("actors/group0.c")
model = read("actors/mario/model.inc.c")

# The two private action IDs must belong to the expected action groups, and each
# must be dispatched by the stock SM64 group-specific action loop.
ground = re.search(r"#define ACT_RIDING_YOSHI_GROUND\s+(0x[0-9A-Fa-f]+)", sm64)
air = re.search(r"#define ACT_RIDING_YOSHI_AIR\s+(0x[0-9A-Fa-f]+)", sm64)
assert ground and air
assert int(ground.group(1), 16) & 0x1C0 == 0x40
assert int(air.group(1), 16) & 0x1C0 == 0x80
assert "case ACT_RIDING_YOSHI_GROUND:" in moving
assert "case ACT_RIDING_YOSHI_AIR:" in airborne
assert "perform_ground_step(m)" in moving[moving.index("s32 act_riding_yoshi_ground"):]
assert "perform_air_step(m, 0)" in airborne[airborne.index("s32 act_riding_yoshi_air"):]
assert "INPUT_A_PRESSED" in moving[moving.index("s32 act_riding_yoshi_ground"):]
assert "INPUT_Z_PRESSED" in moving[moving.index("s32 act_riding_yoshi_ground"):]
assert "INPUT_Z_PRESSED" in airborne[airborne.index("s32 act_riding_yoshi_air"):]

# Yoshi is a separate actor/model in the castle grounds; the original rooftop
# Yoshi definition and star-gated behavior remain present.
assert "const BehaviorScript bhvRideableYoshi[]" in behavior_data
assert "LOAD_ANIMATIONS(oAnimations, yoshi_seg5_anims_05024100)" in behavior_data
assert "bhv_yoshi_init" in behavior_data and "bhv_yoshi_loop" in behavior_data
assert "MODEL_YOSHI" in level and level.count("bhvRideableYoshi") == 1
assert "#if defined(IOS_BUILD)\n    // Test mount is intentionally iOS-only" in level
assert "bhv_rideable_yoshi_loop" in yoshi
assert "mario_ios_is_riding_yoshi(m)" in yoshi
assert "mario_ios_detach_yoshi" in mario

# UIKit only enqueues game-thread requests. The appearance is deliberately
# identified as a temporary palette using Mario geometry and existing animation.
assert "mario_ios_request_character_switch();" in ui
assert "mario_ios_request_yoshi_toggle();" in ui
assert "__atomic_exchange_n(&sIOSGameplayRequests" in mario
assert "temporary green palette on Mario’s model" in ui
assert "static Lights1 mario_red_lights_group" in model
assert "mario_set_temporary_luigi_palette" in actor
assert "sTemporaryLuigiPaletteEnabled = !sTemporaryLuigiPaletteEnabled" in mario
assert "mario_set_temporary_luigi_palette(sTemporaryLuigiPaletteEnabled)" in mario

print("iOS character switch, mount/dismount, action dispatch, and fallback source checks passed")
