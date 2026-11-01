# iOS portal gun prototype

## Current scope

The iOS build has two independently placeable wall portals. Use the native
**Diagnostics → Gameplay** menu to fire blue and orange portals toward the
center of the active camera view, enable/disable portal traversal, or reset
both portals. Placement succeeds only on a static, near-vertical collision
triangle large enough to contain the entire rectangular aperture. Dynamic
object surfaces, floors, ceilings, and edge placements are rejected. Re-firing
one color replaces that portal.

When both portals exist, Mario or the temporary green-palette Mario can cross
the matching wall aperture while moving into its front face. The original level
surface remains intact. The collision system skips only the matching triangle
for Mario's capsule while the capsule fits inside that portal; other actors and
all other wall areas keep normal collision. The player position, velocity,
sliding velocity, facing/input yaw, and camera history are transformed into the
destination frame. A short cooldown prevents immediate bounce-back. Portal
traversal is disabled while mounted on Yoshi.

## Coordinate and transform convention

SM64 uses `+Y` as up; horizontal movement is in X/Z. Yaw zero faces `+Z`, and
positive yaw turns toward `+X`. Each portal stores a world-space center and a
unit outward normal. Its local right vector is `cross(+Y, normal)`, and local
up is world `+Y` for the wall-only version.

For a source-relative point or vector, coordinates are `(x, y, z)` in the
source portal's `(right, up, normal)` basis. Traversal applies
`(-x, y, -z)` in the destination basis. The 180-degree turn maps motion toward
the source plane to motion away from the destination plane while preserving
vector magnitude. Exit position adds a 72-unit clearance along the destination
normal. Camera current, target, and render positions use the same point mapping
to avoid one-frame snapping across the level.

Ray placement uses the existing `find_surface_on_ray` collision-world query,
whose static and dynamic surfaces are partitioned into 16×16 X/Z cells. The ray
origin is the rendered Lakitu camera position and its direction is the camera
focus vector. This keeps aiming aligned with the view without adding Metal
rendering assumptions.

## Rendering status

Portals currently use an animated, tinted aperture mesh rendered as an ordinary
SM64 object through the existing graphics backend. The center color is a
temporary fallback: it does **not** show the linked scene. Offscreen portal
cameras, clipping planes, recursive views, and floor/ceiling portals are not
implemented. The main Metal presentation path is unchanged.

The aperture-fit test is deliberately conservative: all four corners must fit
inside the single hit collision triangle. Some visually broad walls split into
smaller triangles and may reject placements near seams; the next improvement
would validate the combined coplanar static surface region, with an explicit
maximum search bound.

## Validation and device steps

Linux tests cover portal frame transforms, momentum magnitude, front-to-back
crossing, edge rejection, and aperture bounds. Source guards check collision
filtering, game-thread controls, camera transformation, Yoshi gating, and the
render fallback disclosure. Apple compilation is required for the UIKit and
Metal app integration.

On iPhone: install the test IPA; enter a level with a wide wall; use the camera
to aim at a broad static wall and fire blue, then aim at a second broad wall and
fire orange. Approach the blue portal straight on with the joystick. Verify
that Mario exits the orange aperture moving outward, that the camera follows,
and that walking alongside either aperture still collides with its wall. Repeat
while jumping, test the Yoshi-mounted exclusion, then replace each portal and
reset them. Build success and the host math tests do not establish that this
works on the A16 device.
