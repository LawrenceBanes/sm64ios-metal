# Mission 51 gameplay prototype

This change adds opt-in iOS gameplay prototypes without changing save data or the renderer. The feature is implemented in the game action/object systems and has **not yet been validated on an iPhone**.

## Test controls

1. Open the small **Diagnostics** tab at the top of the game, then expand the panel and choose **Gameplay**.
2. **Toggle Mario / temporary Luigi look** switches the shared Mario model's red shirt/cap lighting to green and back. Mario remains the default when the process starts.
3. In Castle Grounds, the separate test Yoshi spawns near the intro position. Walk up to him and tap **A** to mount. The virtual joystick turns and moves the mounted pair; **A** jumps and **Z** dismounts. The Gameplay menu can also request a mount/dismount while in range.

The original rooftop Yoshi behavior remains unchanged. The test mount is only placed in Castle Grounds and is compiled into the level script only for `IOS_BUILD`.

## Scope and limitations

- Luigi is deliberately labeled temporary: this uses Mario's geometry and animations with a green palette. No separate licensed Luigi model or Luigi-specific animation set was present in the project, so no external model or texture was copied.
- Yoshi uses the project's existing Yoshi model and animations. Mario's ground and airborne collision steps remain authoritative, with explicit `ACT_RIDING_YOSHI_GROUND` and `ACT_RIDING_YOSHI_AIR` actions. Mario's collision position and camera target stay at normal gameplay coordinates; only his rendered model is raised onto Yoshi.
- The prototype has no Yoshi tongue, special enemy interactions, unique damage animation, or custom collision capsule. Damage/death/action transitions release the mount; save data is not changed.
- Linux source checks and Apple compilation can establish integration and compilation, but cannot prove that palette changes, mount placement, animation alignment, and control feel are correct on the A16 device. Capture screenshots/logs after device testing before calling the feature visually complete.

## Provenance

Only SM64 actors, behavior conventions, and animation IDs already included in this repository are used. No community code or third-party character assets were imported.
