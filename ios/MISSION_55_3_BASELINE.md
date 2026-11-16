# Mission 55.3 baseline — Connected Kingdom

Verified locally on 2026-11-15 before the Mission 55.3 changes in this checkout.

## Repository and build state

- Repository: `/home/larry/Projects/sm64ios-metal`
- Branch: `ios`
- Starting commit: `b183638b8e8d67d586af072434063faef8063648`
- Historical commits `0253c162`, `913c3b3b`, and `c456f2fb` are present in the local history. The checkout was clean before this mission's edits.
- No GitHub Actions workflow was dispatched for this mission. The project has Apple validation and IPA workflows, but the present CachyOS host has no `xcodebuild`, `xcrun`, iOS SDK, Metal compiler, asset catalog tools, or Apple signing tools. `ld64.lld` alone is present; it is not a substitute for the SDK and frameworks.
- No IPA exists in the repository. No physical device build or test is available from this host.
- The user-supplied `/home/larry/Downloads/oot.o2r` is a local ZIP archive and remains outside Git. ROMs, extracted Nintendo assets, and generated importer outputs are ignored or kept under local build directories.

## Existing game and region architecture

- The app uses the native iOS SDL/UIKit host and `src/pc/gfx/gfx_metal.m` for Metal rendering. The standard SM64 game loop and level architecture remain the gameplay path.
- Castle Grounds is the default recognizable hub. With `IOS_HYRULE_WORLD=1`, its optional wooded trail is authored in the same area with SM64 `Gfx`, `Collision`, and tree objects.
- The trail currently ends at a `bhvFadingWarp` to `LEVEL_HYRULE_FIELD`; a paired Hyrule warp returns to Castle Grounds. These are level transitions, not continuous travel. Kart ownership is area-local, so the current warp respawns a recoverable kart rather than preserving its mount and momentum.
- `levels/hyrule_field` is an opt-in level using the stock `MARIO` object, `TERRAIN`, camera/object systems, and `lvl_init_or_update`. It does not use a Hyrule-only controller. The ordinary build does not add this level to the level table.
- Kokiri Forest (`spot04`) is not registered in the game. The host importer can parse a default scene subset, but it does not yet select alternate headers, resolve all segmented display-list references, or build a playable level. There is no runtime region manager or shared-world coordinate layout.
- Existing source contains iOS-specific Luigi, Yoshi, kart, and portal code, but this audit does not independently verify those features on hardware.

## Revalidated source assets

The local O2R is readable and has 38,390 ZIP entries. On the starting tree, the importer emitted default Hyrule Field geometry/collision and the Castle gateway; its exact fresh conversion and Kokiri findings are recorded in `HYRULE_IMPORT_AUDIT.md` and `KOKIRI_FOREST_AUDIT.md` after this mission's revalidation.

## Mission boundary

The verified baseline is a selectable Hyrule level with a warp-based Castle gateway, not a connected world. A continuous route requires resolving the remaining source display-list dependencies, choosing and validating a shared coordinate layout, integrating multiple region meshes and collision into one active SM64 area, and testing that combined area on iPhone. No source state or historical report is treated as proof of those outcomes.
