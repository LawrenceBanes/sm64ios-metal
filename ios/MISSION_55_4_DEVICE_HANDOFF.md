# Mission 55.4 device handoff — First Footstep

## Build

This is an opt-in Castle Grounds experiment. On a Mac with Xcode 27.x, the
iPhoneOS 27.x SDK, GNU Make 4+, `gobjcopy`, the pinned local SDL2 checkout, the
already-extracted USA SM64 assets, and Larry's local `oot.o2r`, build an
unsigned IPA with:

```sh
IOS_CONNECTED_WORLD_TEST=1 ./ios/build_local_macos_ipa.sh \
  /path/to/local/SDL2 /path/to/oot.o2r \
  build/local-iphoneos/sm64ios-connected-forest-unsigned.ipa
```

The current Mac helper also enables the pre-existing optional Hyrule level
because it imports the supplied archive. The forest test is a side branch in
Castle Grounds; the legacy Hyrule fading warp remains at the far end of the
straight trail and is not part of this route. Do not continue straight to that
endpoint while testing. The normal build leaves `IOS_CONNECTED_WORLD_TEST=0`.

No GitHub Actions workflow was started. Linux host validation cannot compile
the iOS application or Apple's Metal backend, so no IPA was produced on
CachyOS in this mission.

## In-game route

Start in Castle Grounds and follow the existing wooded path at the western
edge toward negative X. Its original-ground join is approximately
`(-7400, 260, 1460)`. At about `x=-9200`, before the old trail endpoint, turn
toward positive Z through the open side of the path. Walk north across the
small green forest clearing to about `z=4200`, then turn around and return the
same way.

Expected behavior: Castle Grounds and the green extension are visible through
the same area render tree; the original `MARIO` and `lvl_init_or_update` path
continues; Mario remains in Castle Grounds area 1; the same terrain collision
stream covers both sides of the join; there is no warp, fade, area load, or
player/camera reset on this branch route.

The extension is deterministic vertex-colored test geometry, not imported
Kokiri Forest or OoT geometry. It is a simple level clearing bounded by
collision walls, not a finished forest scene. Kart and Yoshi clearance has
not been tested.

## Collecting useful evidence

On-device validation should capture the app's persistent diagnostic log after
walking out and back. Check for one Castle Grounds area activation at startup,
the `Connected forest test active in existing area` and collision-load records,
and no additional level/area transition records during the route. Record
short video showing the castle-side approach, branch opening, clearing, and
return. A log without a video cannot prove that the Gfx list produced visible
pixels; a video without a log cannot prove the engine retained the same area.

Report any missing floor, seam drop, invisible mesh, wall collision, camera
pop, or unexpected warp with the diagnostic log and the approximate route
coordinates. Do not treat successful IPA validation as proof of gameplay.
