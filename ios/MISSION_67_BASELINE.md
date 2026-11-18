# Mission 67 baseline

Verified in the isolated `mission-67-native-scene-foundation` worktree, based
on `a173f28b` (`Diagnose missing Hyrule terrain textures with opaque fallback`).
The original worktree and `ios` branch were not changed.

## Current implementation

- `tools/hyrule_importer.py` reads O2R records and lowers one selected scene
  prefix into level-specific SM64 `Gfx`, `Collision`, spawn metadata, and
  optional external texture files. It is not a multi-scene package importer.
- `IOS_HYRULE_WORLD`, `IOS_CONNECTED_WORLD`, and
  `IOS_CONNECTED_WORLD_TEST` are opt-in build paths. The Castle connected
  world is composed into its existing area; the default build stays on the
  original level tables and collision path.
- `src/engine/world_region.c` registers independent static collision meshes
  in the legacy surface partition and exposes region bounds, neighbors,
  activation, deactivation, and conservative distance culling. It is not a
  scene/room file loader and does not own render resources.
- Standard SM64 `Area` and `LevelScript` loading remains the live gameplay
  path. Existing level/area warps reload level state; there was no provider
  contract for loading a scene package while leaving the active gameplay
  state under caller control.
- Existing Hyrule content remains an optional single-level integration. This
  mission did not process the local O2R archive or generate new game assets.

## Baseline validation evidence

The base commit and branch were recorded before edits. The source inspection
above is a code-path audit. No iOS SDK, Metal compilation, IPA, or physical
device result is implied by this baseline.
