#!/usr/bin/env python3
"""Guard the game-thread, UIKit, physics, and object-list Reality Debugger seams."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(path, *needles):
    source = (ROOT / path).read_text()
    for needle in needles:
        assert needle in source, f"{path}: missing {needle!r}"
    return source


header = require("src/game/ios_reality.h", "IOS_REALITY_CHEAT_COUNT 17",
    "IOS_REALITY_WORLD_ROTATION", "IOS_REALITY_RECURSIVE_MARIO",
    "IOS_REALITY_POSSESSION", "IOS_REALITY_NON_EUCLIDEAN_CASTLE",
    "ios_reality_request_toggle", "ios_reality_process_requests",
    "ios_reality_serialize", "ios_reality_deserialize")
registry = require("src/game/ios_reality.c", "CHEAT_BIT(id)",
    "IOS_REALITY_RESULT_CONFLICT", "IOS_REALITY_RESULT_UNAVAILABLE",
    "IOS_REALITY_DEF_ACTION", "__atomic_exchange_n",
    "reality_apply_policy", "IOS_REALITY_SAVE_BYTES")
mario = require("src/game/mario.c", "ios_reality_process_requests();",
    "mario_ios_process_reality_actions(gMarioState);", "ios_reality_global_policy()->swim_anywhere",
    "sIOSRealitySpawnedObjects[8]", "mark_obj_for_deletion(object)")
step = require("src/game/mario_step.c", "policy->zero_gravity", "policy->gravity_multiplier < 0.0f")
objects = require("src/game/object_list_processor.c", "policy()->freeze_enemies",
    "policy()->time_stop", "INTERACT_BOUNCE_TOP")
ui = require("src/pc/gfx/gfx_uikit.m", "showRealityDebugger", "Reality Debugger",
    "ios_reality_request_toggle", "ios_reality_request_action", "unavailable")

assert "Requires a shared transform for rendering, collision, camera, and input" in registry
assert "Requires an isolated recursive scene render" in registry
assert "Requires a safe player and enemy AI control handoff" in registry
assert "Requires paired collision spaces and linked-world rendering" in registry
assert "ios_reality_global_policy()->mario_scale" in mario
assert "ios_reality_global_policy()->jump_multiplier" in mario
assert "ios_reality_global_policy()->friction_multiplier" in (ROOT / "src/game/mario_actions_moving.c").read_text()
assert "TIME_STOP_ENABLED) || ios_reality_global_policy()->time_stop" in objects
assert "Metal" not in header  # Gameplay policy remains independent from rendering.
print("Reality Debugger gameplay, compatibility, and UIKit wiring checks passed")
