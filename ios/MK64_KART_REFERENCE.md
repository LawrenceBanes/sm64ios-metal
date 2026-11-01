# MK64 driving reference and SM64 adaptation

Reference snapshot: [`n64decomp/mk64` commit `58cfcb022e10f83bc3b889d7e97508cae6837098`](https://github.com/n64decomp/mk64/tree/58cfcb022e10f83bc3b889d7e97508cae6837098).

## Source areas reviewed

- [`src/player_controller.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/player_controller.c) contains the frame controller, progressive acceleration, ground steering, powerslide state, hop handling, drift charge, boost release, and wheel/surface sampling. Relevant routines include `player_accelerate`, `func_80033AE0`, `update_drift_state_counter`, `func_8002A79C`, and `kart_hop`.
- [`src/data/kart_attributes.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/data/kart_attributes.c) supplies character/vehicle tuning values consumed by that controller.
- [`src/render_player.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/render_player.c) and [`src/camera.c`](https://github.com/n64decomp/mk64/blob/58cfcb022e10f83bc3b889d7e97508cae6837098/src/camera.c) tie driver/kart display lists, wheel presentation, and race-path cameras to MK64's renderer and track system.

The MK64 implementation is not a standalone physics library. Its vehicle state is threaded through MK64-specific player structs, race track/path data, tire contacts, item/race state, animation/display-list tables, and camera code. This implementation copies no MK64 source, models, or textures. GitHub did not report a recognized repository license during the audit, so the decompilation is used as a behavioral reference only.

## Behavior adapted

The portable controller in [`src/game/ios_kart.c`](../src/game/ios_kart.c) advances once per SM64 simulation tick and owns heading, forward/lateral speed, yaw response, drift charge, and temporary boost. It adapts these observed MK64 patterns:

- Progressive throttle acceleration and separate braking/reverse response.
- Steering response that changes with speed, with weaker airborne steering.
- Drift entry requiring steering input, lateral slip while powersliding, and charge earned while maintaining the drift.
- A short speed boost on drift release after the charge threshold.
- A distinct hop event while grounded.

SM64-specific integration owns collision and world state. Ground and air actions pass movement through `perform_ground_step` and `perform_air_step`; the four quarter-steps inside those functions limit high-speed tunneling and retain ordinary SM64 floor, wall, ramp, and landing behavior. The current floor normal and slippery-surface classification adjust gravity-aligned slope acceleration and traction. A wall response removes velocity into the collision normal. The Mario heading drives the existing chase camera. There is no race path, tire-contact model, item system, or MK64 camera dependency.

## Current gameplay contract

- The iOS-only low-poly placeholder actor is original project geometry. Castle Grounds has one test kart near the intro area; the native Gameplay panel can spawn or reposition the same kart near Mario in other areas after a safe floor check.
- Press **L** near the kart to mount or dismount. While driving, the joystick steers and can provide forward/reverse throttle, **B** accelerates, **Z** brakes and reverses, and **A** hops; holding **A** while steering powerslides. Releasing a charged drift activates the mini-turbo.
- Mario's regular movement state is active only while dismounted. The existing Yoshi attachment, Luigi appearance toggle, portal actions, save format, and Metal renderer remain separate. Kart portal traversal is disabled. Death/action interruption and out-of-bounds recovery release the driver and restore the kart to its last safe ground position.
- No licensed MK64 driver/kart assets were added. The test actor has no independent wheel animation or seated character animation yet; Mario reuses the riding-shell pose while attached.

Future race mode can consume the controller's position, heading, speed, grounded state, drift/boost events, and reset path without moving race/checkpoint/item rules into the physics module. Track checkpoints and race progress should be a separate race-state owner; render/audio effects should consume controller events rather than change simulation rules.
