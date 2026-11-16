# Mission 55.3 device handoff

## Current build status

- Source baseline for this work: `b183638b8e8d67d586af072434063faef8063648` plus the local Mission 55.3 changes recorded in `MISSION_55_3_REPORT.md`.
- A host-side preflight and Linux tests are available; **no Hyrule-enabled iOS IPA has been built from this work**.
- No IPA is available to install. Do not use a previous Hyrule build as proof of these changes.
- The user has not authorized an installation operation. This handoff only describes the later test steps.

## Inputs required on a Mac

1. This source checkout and its private local extracted SM64 assets.
2. Larry's local `oot.o2r` archive.
3. A local SDL checkout containing revision `5d249570393f7a37e037abf22cd6012a4cc56a71`.
4. macOS with Xcode 27.x and iPhoneOS SDK 27.x, plus GNU Make, `gobjcopy`, and Python 3.

Do not commit or upload ROMs or extracted Nintendo assets. Keep build logs and generated files private.

## Build

Run on the Mac from the repository root:

```sh
./ios/build_local_macos_ipa.sh /path/to/SDL /path/to/oot.o2r \
  build/local-iphoneos/sm64ios-arm64-unsigned.ipa
```

The script checks the Xcode and SDK versions, SDL pin, and asset presence; builds SDL2 and the Hyrule-enabled arm64 app without signing; packages an unsigned IPA; validates its structure/resources; and prints a SHA-256. A successful build still does not establish device rendering or playability.

## Physical-device checks after SideStore signing

1. Install the newly built package using the normal SideStore process.
2. Launch into Castle Grounds and verify ordinary terrain, castle geometry, water, audio, touch controls, save loading, and the existing kart/Yoshi interactions still behave as before.
3. Locate the opt-in forest gateway beyond the wooded western edge. Walk the route from the Castle Grounds join to its destination. Current behavior is a fading level warp; it is not seamless and currently reloads the game level.
4. In Hyrule Field, record whether the imported geometry/textures appear, whether Mario spawns on the intended ground, and whether floor, wall, ledge, jump, camera, and out-of-bounds recovery work.
5. Use the return path to get back to Castle Grounds. Check whether game progress and character selection persist and note that the current kart is respawned by the level rather than carried across the warp.
6. Capture the app's Documents diagnostic log through its existing share sheet and include the launch/transition portion with screenshots or a recording. Do not infer iPhone success from CI or Linux tests.

## Expected limitation

Kokiri Forest is not a runtime level yet. Therefore the requested route Castle Grounds → forest passage → Kokiri Forest → Hyrule Field → Castle Grounds cannot be performed with this source state. The first device test is only Castle Grounds ↔ Hyrule Field via the existing warp path.
