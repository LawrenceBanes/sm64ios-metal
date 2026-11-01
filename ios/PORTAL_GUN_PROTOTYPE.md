# iOS Portal Gun Prototype

## Placement and current scope

Open the native **Diagnostics → Gameplay** menu and fire blue/orange portals.
The ray starts at Lakitu's center-of-screen focus and continues in the camera's
forward direction. A placement can attach to a static wall, floor, ceiling, or
slope when the complete 156×360 aperture has collision coverage. Coverage is
sampled across adjacent, sufficiently coplanar collision triangles, including
the boundary, and nearby collision geometry that intersects the opening is
rejected. Each accepted portal stores a full orthonormal surface frame, so its
display orientation follows the hit normal. Re-firing a color replaces that
portal; an overlapping paired portal is rejected.

Moving-platform anchors are explicitly unsupported for now. The request is
rejected with an on-screen message because the portal frame does not yet follow
dynamic-surface transforms. Other placement rejection reasons include no ray
hit, insufficient surface coverage, a geometry obstruction, and overlap with
the paired portal. Each request has a game-thread consumption log, placement
result log, and short-lived native UIKit feedback.

When both portals are placed, Mario can cross in either direction. Collision
pass-through checks apply to Mario's capsule against matching static coplanar
wall, floor, and ceiling triangles; the level collision mesh is unchanged.
Traversal maps the player center, velocity, sliding velocity, facing and
camera history through the source/destination bases. Exit placement includes
capsule clearance along the destination normal and is checked against wall,
floor, and ceiling collision before it is committed. A short cooldown prevents
immediate re-entry. Portal traversal is disabled while mounted on Yoshi.

The center remains a tinted animated surface fallback. It does **not** show the
linked world. Recursive or offscreen portal-camera rendering is not part of
this prototype. Normal SM64 display-list geometry and the existing Metal
presentation path draw the visible portal meshes.

## Coordinate convention

SM64 uses +Y as up and X/Z as the horizontal plane. A `PortalFrame` stores a
world-space center and an orthonormal local right/up/normal basis. The normal
points toward the side from which the player approaches. The local up axis is
the projection of world up into the surface plane, with the camera direction
providing stable roll for near-horizontal surfaces. For a source-relative
point or vector, traversal applies `(-right, +up, -normal)` in the destination
basis. This half-turn sends incoming normal motion outward while preserving
momentum magnitude. Mario remains upright; his 3D velocity and camera are
transformed, while his facing yaw is projected back onto SM64's horizontal
movement plane.

## Diagnostics and tests

The persistent `SM64Diagnostics.log` now records, on events rather than every
frame: queued-request consumption, ray origin/direction and collision result,
rejection/success reason, accepted frame basis, actor visibility, matching
portal-colored Metal triangle batches, traversal attempt, exit rejection, and
successful transformed position/velocity. Blue/orange status toasts appear in
the native overlay. A Metal draw marker indicates that portal-colored vertices
reached an encoded game draw; it does not independently prove visible pixels.

Linux tests cover wall/floor/ceiling crossing direction, sloped-frame
orthogonality, wall/floor transform momentum, capsule aperture margins, union
coverage across a multi-triangle wall, seam acceptance, gap rejection,
near-coplanar tolerance, and exit collision/floor safety. Source checks verify
all three collision classes and the event chain. Bob-omb Battlefield, castle
grounds, actual floor/ceiling traversal, visible portal geometry, and A16
runtime collision still require on-device testing.
