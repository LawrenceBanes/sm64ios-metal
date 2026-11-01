#!/usr/bin/env python3
"""Source guards for scene-backed, surface-oriented iOS portals."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text()


mario = read("src/game/mario.c")
mario_h = read("src/game/mario.h")
collision = read("src/engine/surface_collision.c")
ui = read("src/pc/gfx/gfx_uikit.m")
metal = read("src/pc/gfx/gfx_metal.m")
scripts = read("levels/scripts.c")
behavior = read("data/behavior_data.c")
model = read("actors/portal/model.inc.c")
math_h = read("include/game/portal_math.h")

# UIKit only enqueues requests; the game action atomically consumes them and
# records the consumed mask before raycast or state mutation.
assert "__atomic_fetch_or(&sIOSGameplayRequests" in mario
assert "__atomic_exchange_n(&sIOSGameplayRequests, 0, __ATOMIC_ACQUIRE)" in mario
assert "Portal requests consumed by game thread" in mario
assert "Gameplay menu queued blue portal placement" in ui
assert "Gameplay menu queued orange portal placement" in ui

# Aim from the center-of-screen focus point along the true camera forward ray.
assert "vec3f_copy(rayOrigin, gLakituState.focus)" in mario
assert "gLakituState.focus[0] - gLakituState.pos[0]" in mario
assert "find_surface_on_ray(rayOrigin, rayDirection, &surface, hitPosition)" in mario
assert "Portal placement rejected type=%s reason=no collision hit" in mario
assert "moving-platform anchors are not yet supported" in mario

# Placement validates the whole opening against a union of static coplanar or
# near-coplanar collision triangles, with edge/gap and nearby-obstacle checks.
assert "mario_ios_portal_aperture_has_support" in mario
assert "mario_ios_portal_point_has_static_support" in mario
assert "portal_triangle_matches_frame" in mario
assert "mario_ios_portal_has_nearby_obstacle" in mario
assert "collision footprint has an edge or gap" in mario
assert "portal_aperture_covered_by_triangles" in math_h
assert "fabsf(surface->normal.y) > 0.10f" not in mario
assert "portal_aperture_fits_triangle" not in mario

# The stored orthonormal surface frame drives model orientation, arbitrary
# normal traversal, capsule aperture bounds, and momentum transformation.
assert "portal_frame_set_basis(&candidate, surfaceNormal, preferredUp)" in mario
assert "angle[0] = atan2s(-frame->normal[1], horizontal)" in mario
assert "angle[2] = atan2s(-portal_dot3(frame->up, baseRight)" in mario
assert "portal_capsule_axis_extent(to->normal" in mario
assert "portal_transform_point(from, to, currentCenter" in mario
assert "portal_transform_vector(from, to, m->vel" in mario
assert "portal_exit_correction_is_safe(requestedPosition, attemptedPosition" in mario
assert "portal_exit_resolve_floor(&attemptedPosition[1]" in mario
assert "sIOSPortalTraversalCooldown = 6" in mario
assert "velocityToward < -0.5f" in mario

# Mario's collision filter applies to wall, floor, and ceiling partitions; it
# never changes the level collision data or allows non-Mario actors through.
assert collision.count("mario_ios_portal_surface_passable(surf, x, y, z,") >= 3
assert "gCurrentObject == gMarioObject" in collision
assert "mario_ios_surface_matches_portal(surface, frame)" in mario
assert "gCheckingSurfaceCollisionsForCamera" in mario
assert "mario_ios_is_riding_yoshi(m)" in mario

# Each rejection reaches a short-lived native toast; successful actor
# visibility and actual Metal draws have separate event markers.
assert "void gfx_ios_portal_status(const char *message)" in read("src/pc/gfx/gfx_ios_diagnostics.h")
assert "showPortalStatus:" in ui
assert "Portal actor visible type=%s" in mario
assert "Metal portal geometry draw encoded type=%s" in metal
assert "Portal traversal crossing source=%s" in mario
assert "Portal traversal succeeded source=%s" in mario
assert "Portal traversal rejected source=%s" in mario

for name in ("blue", "orange", "enable_toggle", "reset"):
    assert f"mario_ios_request_portal_{name}" in mario_h
    assert f"mario_ios_request_portal_{name}" in ui
assert "not a linked world view" in ui
assert "MODEL_PORTAL_BLUE" in scripts and "MODEL_PORTAL_ORANGE" in scripts
assert '#include "behavior_data.h"' in scripts
assert "bhvPortalBlue" in behavior and "bhvPortalOrange" in behavior
assert "LAYER_TRANSPARENT" in model

print("iOS portal request, universal surface placement, traversal, feedback, and render-path checks passed")
