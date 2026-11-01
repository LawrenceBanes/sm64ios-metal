# MK64 driving reference and SM64 adaptation

Reference snapshot: [`n64decomp/mk64` commit `58cfcb022e10f83bc3b889d7e97508cae6837098`](https://github.com/n64decomp/mk64/tree/58cfcb022e10f83bc3b889d7e97508cae6837098).

## Source areas reviewed

- [`src/player_controller.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/player_controller.c) contains the frame controller, progressive acceleration, ground steering, powerslide state, hop handling, drift charge, boost release, and wheel/surface sampling. Relevant routines include `player_accelerate`, `func_80033AE0`, `update_drift_state_counter`, `func_8002A79C`, and `kart_hop`.
- [`src/data/kart_attributes.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/data/kart_attributes.c) supplies character/vehicle tuning values consumed by that controller.
- [`src/render_player.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/render_player.c) and [`src/camera.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/camera.c) tie driver/kart display lists, wheel presentation, and race-path cameras to MK64's renderer and track system.

The MK64 implementation is not a standalone physics library. Its vehicle state is threaded through MK64-specific player structs, race track/path data, tire contacts, item/race state, animation/display-list tables, and camera code. This implementation copies no MK64 source, models, or textures. GitHub did not report a recognized repository license during the audit, so the decompilation is used as a behavioral reference only.

## Source comparison and Mission 53.1 corrections

The first SM64 kart pass was a custom approximation, not an accurate port of MK64's controller. The decompilation's actual behavior is more stateful:

- `player_accelerate` uses ten speed bands and character-specific acceleration tables, with separate multipliers near the top speed. The SM64 controller now uses a smooth speed-dependent curve; per-character MK64 tables are intentionally not imported.
- Ground steering in `func_80033AE0` consumes a dead-zoned X axis, advances a steering position with a changing resistance/increment, accounts for turn direction and tire-surface samples, and has separate spinout/drift/hop state. The old SM64 pass applied joystick directly to yaw and multiplied by the sign of vehicle speed. That made steering reverse the nose while backing up. The current mapping uses a damped steering angle and yaw rate; steering right continues to turn the nose right in reverse.
- `update_drift_state_counter` derives charging from the signed sideways/drift state and steering position, then promotes charge in stages after sustained frames. `func_8002A79C` only starts the mini-turbo once drift state reaches its threshold and runs it for 31 frames. The first SM64 pass charged any held A+steer sequence without retaining turn direction. Mission 53.1 records the drift direction, requires sufficient speed and sustained steering in that direction, develops lateral speed toward the outside of the corner, and only boosts after a charged release. This is a deliberate one-stage simplification of MK64's thresholds.
- `kart_hop` uses character-specific hop jerk/initial velocity and a separate hop/gravity state. SM64 keeps its existing ground/air collision and jump integration instead of importing that state machine.

The Mission 53.1 control audit follows the actual touch pipeline. UIKit X displacement is positive to screen-right and UIKit Y is positive downward. `controller_touch_axis.h` converts directly to signed N64 axes (+X right, +Y up), avoiding the prior unsigned-centered arithmetic followed by narrowing to `s8`. The kart adapter maps +Y to forward throttle, -Y to an explicit reverse request, B to forward throttle, and Z to service brake only. Heading zero is the SM64 +Z direction; positive yaw points toward +X. Steering is vehicle-relative, independent of camera yaw. A hill-normal projection also had its gravity sign reversed and now accelerates downhill.

The compact controller is deterministic per SM64 gameplay tick. It is intended to be intuitive rather than reproduce MK64 character tables, surface-specific tire simulation, race-path logic, or its camera. Device telemetry records raw/shaped axes, mapped controls, kart nose/velocity, yaw, and camera yaw so an iPhone log can distinguish a touch inversion from a world-heading or camera mismatch.

## Behavior adapted

The portable controller in [`src/game/ios_kart.c`](../src/game/ios_kart.c) advances once per SM64 simulation tick and owns heading, forward/lateral speed, yaw response, drift charge, and temporary boost. It adapts these observed MK64 patterns:

- Progressive throttle acceleration and separate braking/reverse response.
- Steering response that changes with speed, with weaker airborne steering.
- Drift entry requiring steering input, lateral slip while powersliding, and charge earned while maintaining the drift.
- A short speed boost on drift release after the charge threshold.
- A distinct hop event while grounded.

SM64-specific integration owns collision and world state. Ground and air actions pass movement through `perform_ground_step` and `perform_air_step`; the four quarter-steps inside those functions limit high-speed tunneling and retain ordinary SM64 floor, wall, ramp, and landing behavior. The current floor normal and slippery-surface classification adjust gravity-aligned slope acceleration and traction. A wall response removes velocity into the collision normal. The Mario heading drives the existing chase camera. There is no race path, tire-contact model, item system, or MK64 camera dependency.

## Current gameplay contract

- The iOS-only kart geometry is original project work, with a beveled chassis, bumpers, seat, steering wheel, engine/exhaust, axles, and four transform-animated tires. Castle Grounds has one test kart near the intro area; the native Gameplay panel can spawn or reposition the same kart near Mario in other areas after a safe floor check.
- Press **L** near the kart to mount or dismount. While driving, joystick left/right steers, joystick up or **B** accelerates, joystick down requests reverse, **Z** brakes, and **A** hops/drifts. Hold A and steer in the same direction through a charged slide, then release A to activate the mini-turbo. The iPhone build still needs hands-on validation of these directions and handling.
- Mario's regular movement state is active only while dismounted. The existing Yoshi attachment, Luigi appearance toggle, portal actions, save format, and Metal renderer remain separate. Kart portal traversal is disabled. Death/action interruption and out-of-bounds recovery release the driver and restore the kart to its last safe ground position.
- No MK64 driver/kart assets were added. Mario reuses the available riding-shell seated pose with open hands and shell-riding torso lean; there is no custom steering animation, independent suspension, racing system, or MK64 sound/character tuning table.

Future race mode can consume the controller's position, heading, speed, grounded state, drift/boost events, and reset path without moving race/checkpoint/item rules into the physics module. Track checkpoints and race progress should be a separate race-state owner; render/audio effects should consume controller events rather than change simulation rules.
