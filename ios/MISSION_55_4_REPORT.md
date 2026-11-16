# Mission 55.4 report — Operation: The First Footstep

**Date:** 2026-11-15  
**Branch:** `ios`  
**Starting code HEAD:** `a7d9de10011caaaa0d3a553c69129088943b2d36`  
**Baseline record commit:** `04fa29d7`  
**GitHub Actions / paid CI:** not run  
**IPA / iPhone test:** not produced / not performed

## Outcome

**PARTIAL.** Castle Grounds now has an opt-in, same-area forest branch. Its
render display list is registered beside the existing Castle and gateway
lists. Its floor and walls are appended to the Castle Grounds `Collision`
array consumed by the same single `TERRAIN` command. The extension contains
no warp or level command. Host source checks and C object compilation support
the intended architecture, and a source-triangle sampler verifies the planned
out-and-back floor route.

The host-side acceptance criterion is **partially demonstrated**, not fully
proven: no executable headless SM64 traversal or Apple build was run, so actual
runtime collision response and visible rendering remain unverified. The full
physical-iPhone criterion is **UNVERIFIED**.

## Baseline and placement

The verified starting tree was clean at `a7d9de10` on branch `ios`. The
baseline is recorded in [`MISSION_55_4_BASELINE.md`](MISSION_55_4_BASELINE.md).
The code first present at that revision already had a western Castle trail
whose floor joins sampled original Castle collision at Y=260 near
`(-7400,1460)`. The trail extends along negative X, with a passage across
Z=720..2200 and an existing Hyrule fading warp at its far end.

The test extension leaves the north/positive-Z side of that passage at about
X=-9200. Its open entrance spans the existing corridor edge between X=-9800
and X=-8600 at Z=2200. It widens to X=-10400..-8200 at Z=4200. Floor height
is Y=260, aligned with the corridor. These coordinates stay within the
opt-in signed-16 coordinate range and the 64x64 grid. The route branches well
before the old warp. The original Castle geometry and collision are not
translated or distorted.

## Implementation

- Added `IOS_CONNECTED_WORLD_TEST`, default `0`, to the Makefile. In test mode,
  Castle Grounds includes a vertex-colored clearing/path, frame trees, an
  open corridor side, and the branch collision. No new level or area is
  registered; the stock Mario object and game update loop remain.
- Used the expanded 64x64 collision partition and existing 8,192-surface /
  32,768-node capacities for either `IOS_HYRULE_WORLD` or
  `IOS_CONNECTED_WORLD_TEST`. Ordinary mode retains its stock 16x16 grid and
  original pool capacities.
- Updated build-mode invalidation to track both opt-in flags, including
  generated level headers and Castle area objects. Explicitly propagated and
  validated the connected-world flag through `build.sh` and `build_ios.sh`.
- Added bounded area-load diagnostics for collision counts and active Castle
  area. The Mac IPA helper accepts `IOS_CONNECTED_WORLD_TEST=1`; it continues
  to use its existing Hyrule-enabled private-archive workflow.
- Added a collision source sampler for the approach, corridor, branch seam,
  clearing, and return route. This evaluates the authored collision triangles
  and grid bounds; it is not a call into the running SM64 floor-query engine.

## Validation

**PASS —** `git diff --check`, Python bytecode checks, and shell syntax checks.  
**PASS —** Linux regression suite in the local preflight: 31 importer tests.  
**PASS —** Mission-specific gateway tests: 8/8.  
**PASS —** Collision partition tests: 3/3.  
**PASS —** IPA/asset-checksum host tests: 16/16.  
**PASS —** Preflight converted the local O2R archive and confirmed the
existing Hyrule/Kokiri importer outputs; it created and removed its temporary
SM64 asset bundle.  
**PASS —** Host C compilation of Hyrule-enabled shared objects, connected-only
Castle objects, then ordinary Castle objects in one reused build directory.
The final switch recompiled Castle geometry/scripts, collision grid objects,
Mario/area, generated level headers, and level scripts.  
**PASS —** Test configurations compiled with flags off, Hyrule-only,
connected-only, and both options for the targeted collision/script sources.
The connected path's collision triangles are within the configured pool and
the source sampler found Y=260 along the planned out-and-back samples.  
**UNVERIFIED —** Xcode, iPhoneOS SDK, Apple's Metal compiler, IPA creation,
and physical rendering. The CachyOS host has no `xcodebuild` or `xcrun`; the
preflight explicitly reported that no IPA was built.  
**UNVERIFIED —** Real SM64 collision queries, camera behavior, uninterrupted
player state, visible pixels, and physical traversal.  
**NOT RUN —** GitHub Actions, due to the user's no-paid-CI restriction.

The suite emitted an existing warning in `mario_actions_airborne.c:75` about
a tautological comparison. Connected-world source compilation also emitted
the existing unused-parameter warning in `mario.c`; neither file was changed
for this mission.

## Remaining work

1. Build the connected test IPA on a local Mac using the command in
   [`MISSION_55_4_DEVICE_HANDOFF.md`](MISSION_55_4_DEVICE_HANDOFF.md).
2. Install it on the iPhone and perform the out-and-back route while capturing
   video and the persistent diagnostic log.
3. If traversal succeeds, replace the temporary clearing with verified
   imported forest geometry in a later mission. If it fails, use the log and
   exact route point to distinguish render submission from floor/wall-query
   problems.

Do not describe this as a completed seamless-world milestone until the
iPhone route is physically verified.
