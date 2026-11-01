#!/usr/bin/env python3
"""Source guards for the collision-based iOS wall-portal prototype."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text()


mario = read("src/game/mario.c")
mario_h = read("src/game/mario.h")
collision = read("src/engine/surface_collision.c")
ui = read("src/pc/gfx/gfx_uikit.m")
scripts = read("levels/scripts.c")
behavior = read("data/behavior_data.c")
model = read("actors/portal/model.inc.c")
math_h = read("include/game/portal_math.h")

# Placement uses a camera-directed collision-world ray, rejects moving/tilted
# surfaces, and requires the full aperture to fit the host triangle.
assert "find_surface_on_ray(rayOrigin, rayDirection, &surface, hitPosition)" in mario
assert "surface->flags & SURFACE_FLAG_DYNAMIC" in mario
assert "fabsf(surface->normal.y) > 0.10f" in mario
assert "portal_aperture_fits_triangle" in mario

# Original collision geometry remains present; only Mario's collision query is
# allowed to pass the exact two portal host triangles within the aperture.
assert "mario_ios_portal_surface_passable(surf, x, y, z, radius)" in collision
assert "surface != sIOSPortalSurfaces[i]" in mario
assert "!sIOSPortalValid[0] || !sIOSPortalValid[1]" in mario
assert "velocityToward < -0.5f" in mario

# Traversal transforms position, momentum, facing, and camera history, with a
# short re-entry cooldown and a hard Yoshi exclusion.
assert "portal_transform_point(from, to, m->pos" in mario
assert "portal_transform_vector(from, to, m->vel" in mario
assert "portal_exit_correction_is_safe(requestedPosition, attemptedPosition" in mario
assert "pushDistanceSquared" not in mario
assert "mario_ios_transform_yaw(from, to, m->faceAngle[1])" in mario
assert "gLakituState.curPos" in mario and "gLakituState.goalPos" in mario
assert "sIOSPortalTraversalCooldown = 6" in mario
assert "mario_ios_is_riding_yoshi(m)" in mario
assert "sIOSPortalArea != gCurrentArea" in mario
assert "floor == NULL" in mario and "ceilHeight < attemptedPosition[1] + IOS_PORTAL_PLAYER_HEIGHT" in mario

# Native controls expose both placements, enable toggle, and reset, and the
# UI describes the visual fallback honestly.
for name in ("blue", "orange", "enable_toggle", "reset"):
    assert f"mario_ios_request_portal_{name}" in mario_h
    assert f"mario_ios_request_portal_{name}" in ui
assert "not a linked world view" in ui
assert "MODEL_PORTAL_BLUE" in scripts and "MODEL_PORTAL_ORANGE" in scripts
assert "bhvPortalBlue" in behavior and "bhvPortalOrange" in behavior
assert "LAYER_TRANSPARENT" in model

# Unit-testable mapping is the 180-degree portal-through transform, including
# normal component reversal and an explicit exit clearance offset.
assert "output[0] = -localX * targetRight[0] - localZ * to->normal[0]" in math_h
assert "exitOffset * to->normal[0]" in math_h
assert "startDistance <= 0.0f || endDistance > 0.0f" in math_h

print("iOS portal placement, traversal, control, and fallback source checks passed")
