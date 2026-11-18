# Native mods Phase 1 — SuperSlide

Branch `native-mods-phase1-superslide` starts at Mission 63 `0b5f08f2`.
The implementation changes movement, its Cheats / Play controls, and tests.

## Controls and mechanics

Open **Cheats / Play → Cheats — Hat, Flight, Bowser, SuperSlide**, then enable
**SuperSlide**. The slide cap slider ranges from 30 to 300, with a default
of 90 game units per tick. Settings are session-only and disabled by default.

- Run, hold Z and press B to slide kick into SuperSlide.
- Press B during a ground pound to convert it into a slide. It retains the
  speed recorded before the ground pound, with a minimum entry speed of 15.
- Steer with the stick. Downhill floor normals add momentum. Holding against
  the heading brakes on the ground. Air steering adds momentum up to 45,
  then preserves existing higher momentum.
- Press A on ground for a double jump, or in open air for a jump kick.
  Press B on ground to roll out.
- Graze a wall to slide along it. A kicks away with upward velocity 55 and
  a speed bonus of 10. B drops from the wall. Head-on contact knocks Mario back.
- Water contacts above speed 45 skip with upward velocity 17 and cost 10 speed.
  Lava permits a first skip after entry, then requires speed above 45.
  Low-speed water entry resumes the ordinary airborne water-plunge handling.

Flight and the Bowser prototype take priority. A thrown Cappy prevents new
slide entry. During a slide, the hat action whitelist prevents throws and
bounces. Warp, unload, death, missing-floor and toggle-off paths release or
reset the custom state. The normal action dispatcher handles water plunge and
squashing. Portal traversal continues through the existing post-movement hook.

## Native implementation

`mario_superslide.c` owns the single player's momentum history and first lava
skip. It uses the existing floor/wall references, ground and air steps,
animations, particles, sound and action transitions. The airborne dispatcher
owns `ACT_IOS_SUPERSLIDE`. The regular action loop recognizes slide kick
transitions in the same frame. Mario initialization resets the history.

Entry button edges are consumed locally once, so the B that starts a slide
cannot also trigger its exit. Positive entry velocity uses the air step even
at floor height, preserving the initial slide-kick hop. Missing floors,
zero-length vectors and zero-speed braking are guarded. The configured cap is
applied before movement and after downhill acceleration.

## Source and attribution

Adapted from the user's `superslide.lua`, **SuperSlide 1.1 by SkyCaster09**.
The supplied file's SHA-256 is
`79a5c454221f44682ac1ca18744a66d35d1032162a4d14f07c7079fba9cca855`.
The file contains author credit and no explicit license grant.
This local adaptation includes no third-party textures, models or animation
payloads. Distribution permissions for the supplied mod remain unverified.

The upstream engine is an API reference for the source's Lua hooks.
[SM64CoopDX](https://github.com/coop-deluxe/sm64coopdx)

The source's per-network-player tables become one local runtime state. Its
chat help becomes menu instructions, and its host-only slider becomes an
atomic native session setting. SM64's 30 Hz simulation retains the supplied
per-tick tuning.

## Review and validation

Run `CC=clang tests/run_linux_checks.sh` and
`sh tests/check_metal_project_wiring.sh`.

The new unit harness executes the native module with controlled collision
services. It covers both hat compile modes, entries, entry button consumption,
initial hop, downhill speed, braking, steering, cap limits, combo exits,
glancing and head-on walls, water/lava skips, disabled behavior, mode conflicts,
warp/death/unload and missing-floor recovery.

The Castle collision harness runs the actual module and actual surface/step
code against both crest rails at speed 300, with legacy forest mode on/off.
Animation, sound and action initialization are stand-ins in this harness.
The complete suite also checks the normal, Hyrule, connected and combined hat
configurations. These tests do not establish iPhone gameplay feel.

Luna should review action ID isolation, the same-frame entry path, disabled
behavior and exit velocities. Device checks remain for touch chord timing,
camera tracking, floor-aligned animation, audio/particles, high-speed edges,
water/lava and wall-angle timing. Test the slider endpoints, then toggle off
mid-slide and cross a warp. Verify Cappy, Superman and Bowser priority using
the same menu. UIKit/Apple SDK compilation and physical Metal rendering need
an authorized Apple validation run.
