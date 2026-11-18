# Mission 63 cheat playground

Branch `mission-63-cheat-playground` starts at Mission 61 `b859ba02`.
Mission 62 is an independent sibling branch. Integration with its rendering
corrections remains a separate review step.

Open the live iOS panel's **Cheats / Play** button, then **Cheats — Hat,
Superman, Bowser**. The storyboard Cheats screen also has **Open cheat
playground**. Rows have 64-point heights and labelled native controls.

## Implemented

- Hat ON restores normal-cap ownership, clears all four lost-cap save flags
  and removes the old loose cap in the active area. OFF removes head/hand
  visibility and prevents throws. Either change cancels the projectile.
  Normal releases the override. Returning from forced OFF restores the cap.
  Restoring ownership intentionally updates the active save's cap flags.
- Superman flight is a session toggle. Stick direction uses the existing
  camera-relative intended yaw. A rises, Z descends, and releasing both holds
  altitude. Releasing the stick coasts. Steering accelerates toward the input.
  Constants in `mario_playground.inc.c` set acceleration to 2 units/tick²,
  horizontal maximum to 48 units/tick and vertical speed to 24 units/tick.
  The stock four-quarter air collision step handles walls, floors and ceilings.
  Coordinate guards stop thrust outside the collision grid and at Y ±30000.
  Entering water resumes swimming. Turning flight off resumes freefall/gravity.
- Settings use atomic access to an added field of the existing CheatList.
  They default to zero and last for the process session. Runtime activity
  belongs to the game thread and suspends during incompatible actions, death,
  warps, swimming, mounts and held-object sequences. Compatible play resumes
  selected modes. The existing cheat master switch controls the older cheats.
- Cappy's render-only behavior left oOpacity zero after object allocation.
  The cap GeoLayout reads that field for transparency. Launch now sets 255.
  Device visibility and timing still need verification.

## Bowser prototype

The same player object retains Mario's traversal, interactions and collision
capsule. Ground speed is limited to 24 units/tick. The renderer submits the
native Bowser skeleton at quarter scale, with Bowser animation 12 (idle),
13 (slow gait) or 9 (airborne). Mario's animation structure, model and scale
are restored after that render call. Mario animation indices never reach
Bowser's skeleton. The native boss geometry callback skips its child-body
position update for the player, and his eyes use the open-eye variant.

This is a limited animated traversal prototype. Fire attacks, shell moves,
Bowser-sized collision, attack animations, voices and a complete moveset remain
unsupported. Swimming, scripted sequences and mounted play use Mario. The
camera follows the existing player position. Bowser boss AI and extra boss
objects are not spawned.

## Mod research

Character Select supplies a Lua registration API including models, camera
scale, costumes and animation hooks. Its repository lists MIT licensing.
[Character Select API](https://github.com/Squishy6094/character-select-coop/blob/main/z-api.lua)

Extra Characters Plus requires Character Select and supplies per-character
models, caps, palettes, animation overrides and movesets through CoopDX APIs.
Repository-level reuse permission for all contributed art was not established.
[Extra Characters Plus](https://github.com/coop-deluxe/extra-characters-plus/blob/main/main.lua)

OMM Rebirth describes Cappy and unrestricted wing-cap movement. Bowser (+Jr)
describes shell spin, fireball and custom animations, and requires Character
Select. These were design references. This implementation uses native code
and the project's existing assets. No mod code or assets were imported.
[OMM Rebirth](https://mods.sm64coopdx.com/mods/odyssey-marios-moveset-rebirth.125/)
[Bowser (+Jr)](https://mods.sm64coopdx.com/mods/cs-bowser-jr-moveset.36/)

## Validation and Luna review

Run `CC=clang tests/run_linux_checks.sh` and
`sh tests/check_metal_project_wiring.sh`. The suite includes lost ownership,
real projectile cancellation while thrown/returning, warp/unload/death recovery,
1000 flight ticks, speed bounds, coasting, disabling, water and scripted-state
suspension. Collision tests use the actual air step on Castle's crest and rail.
The broader suite covers both hat build modes and Hyrule/connected combinations.

Linux cannot compile UIKit against an Apple SDK or run Metal. Review native
presentation, VoiceOver labels and touch hit targets on iPhone. Exercise A/Z
simultaneously with steering and external controllers. Test flight against
ceilings, walls, water, death planes and the Hyrule seam, then disable while
hovering and while resting on a floor. Check Bowser idle/walk/jump rendering,
camera clearance, model restoration and gameplay interactions. Verify hat
visibility, lost-cap restoration and throws before and after death/warps.
