# Mission 76 — Scale and reusable OoT asset library

## Implementation

Branch `mission-76-oot-asset-library` starts at `11e84a68069d185e27adc8650c292a794489e2a6`, the successful Mission 74/75 IPA source. The old private release `sm64-ios-unsigned-44677072011-1`, original Documents ZIP, and 1.0x package are retained for rollback.

The existing exporter `--scale` applies once to baked render positions, collision vertices and entrances. Authored bounds now pass through that same transform; export reports record the scale and offset explicitly. `--scale 7/4 --expanded-grid` is the experimental 1.75x setting. UVs, colors, angles, material equations and Mario physics remain unchanged. Mario-sized spawn clearance and extra vertical room-query margins remain native SM64 distances. Native camera placement follows the scaled player spawn through the existing camera code; imported Zelda camera metadata is still unsupported and preserved in source only. Fog coefficients describe normalized clip depth and are not dimensional distances to multiply by 1.75.

The scaled Hyrule export has 202 batches, 57 materials, 50 textures, 1,639 render triangles, 1,579 collision triangles and 16 accepted entrances. Source entrances 7 and 14 remain explicitly excluded after clearance checks. Entrances 2 and 3 become safe at the new scale and are retained. Default spawn is `(280,16,2476)`. The floor itself is scaled; the 16-unit spawn lift is native player clearance. Room bounds are `(-19334,-1916,-3071)..(11781,1295,28926)`, within the expanded grid and signed 16-bit coordinates. Extending this placement much further will exceed those limits and must fail explicitly.

## Private library

`python3 -m tools.oot_asset_library SOURCE.o2r OUTPUT.zip --scene-export EXPORTED_SCENE_DIR` creates a deterministic, versioned library. No source path is hardcoded. The actual supplied archive is NTSC 1.0 marker `01 EC 70 11 B7`; SHA-256 `e3bf3ae3565768998026e60113ed3fc506fc5415e23299de41b9b42a5fbc3807`.

All 38,390 original resource entries are retained byte for byte under `oot-assets/source/`. This includes scene/room data, objects, actors, skeletons, animations, textures, palettes, skyboxes, environments, paths, scripts, metadata and audio, including formats unsupported by SM64. Source preservation does not mean those resources execute in the runtime. The master is 47,642,793 bytes compressed and 106,964,618 bytes uncompressed across source, generated data and reports.

The manifest records stable source paths, types, versions, sizes, SHA-256, dependencies, parser status and explicit blockers. The full inventory reports 101 scene roots, 388 room headers, 385 alternate headers, 635 resolved room links, 3 unresolved room links and 1,118 unresolved cross-scene exit slots. Parser coverage: 24,979 parsed, 5,016 partial, 8,395 unsupported. No source entries are omitted. There are 12,655 source textures; 10,221 can be decoded as whole-image non-indexed textures, with content deduplication. 2,434 texture sources retain explicit conversion blockers, chiefly palette interpretation or incompatible dimensions/state. Validated scene exports add required palette-baked variants. The resulting library has 8,306 runtime files including the Hyrule WSC5 package.

Runtime paths use the existing exporter content identities (`gfx/scenes/o2r-SHA256.rgba16.png`). Full images from future supported scenes can reuse those identities. Cropped images or newly resolved palette banks may require a small incremental runtime update. Keeping all source entries on device does not allow the iOS app to convert arbitrary source resources itself; conversion remains host-side.

## Runtime

`fs_load_scene_resource` resolves legacy Documents files first, then a versioned `oot-assets/runtime-index.tsv` derived from the manifest. The index is capped at 4 MiB (actual 1,546,474 bytes); resource reads preserve existing byte caps and PNG dimensions are validated by the production loader. Index/file paths must be safe relative paths in the scene namespaces. Symlink components below Documents are rejected for library reads. Windows library loading is deliberately unsupported pending a reparse-point containment implementation; legacy files still work.

Only the index is cached. PNG decoding/upload remains lazy per active draw/room and uses existing renderer eviction/lifetime rules. Scene teardown and the external menu availability check clear the index for deterministic reload and updates. File sizes are checked at runtime; SHA-256 validation is performed by the host archive builder and supplied checksum, not recalculated for every texture draw. Resource files must not be replaced while a scene is active.

`--installed-library PATH/oot-assets` on the scene exporter omits byte-identical PNGs already present. A new package is still produced. For missing variants, `python3 -m tools.oot_asset_library SOURCE.o2r UPDATE.zip --update-from MASTER.zip --scene-export NEW_EXPORT` produces an overlay containing only changed runtime files plus the merged manifest/index. It preserves the original source library. Use the newest master/overlay manifest as the baseline when preparing subsequent updates; the present CLI accepts a master ZIP baseline. Multiple independent overlays against an obsolete master must not be merged blindly.

## Device installation

1. Install the Mission 76 unsigned IPA using the established SideStore/LiveContainer process and launch once.
2. Download the private `OoT-Complete-Asset-Library-v1.zip` in Files and extract it.
3. Move the extracted **oot-assets** folder into the app's Documents folder. It must contain `runtime-index.tsv`, `scenes/4100.wsc`, `gfx/scenes/`, `source/`, `manifest.json`, and `inventory.json`.
4. If Mission 74 files already exist, move `Documents/scenes/4100.wsc` and its old `gfx/scenes` folder into a backup folder outside those paths. Legacy files intentionally take precedence over library files; leaving the old package in place loads the old scale.
5. Enter Castle Grounds, then Gameplay → Load Hyrule Field (WSC5). Walk, jump, inspect fences/structures, move the camera, leave and re-enter. Capture the diagnostics log and video.
6. Apply future runtime overlays only while outside the external scene, replacing the index after the files are copied; reopen the menu to reload it.

## Validation boundary

Full Linux regressions, original-fixture library determinism/source-byte preservation/incremental tests, scaled geometry/material/UV tests, filesystem size/path/version/symlink checks, and ASan/UBSan WSC lifecycle tests pass. The authentic scaled scene loaded through the library in the host probe at `(280,16,2476)`, with 1,579 collision triangles, a valid floor, draw submission and texture upload. That probe uses a synthetic camera and does not establish actual Metal pixels or camera quality. Physical-device scale, camera clipping, performance, and visual fidelity remain unverified until Larry tests the new IPA.
