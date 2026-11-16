# Mission 55.3 report — Connected Kingdom

**Status date:** 2026-11-15
**Starting commit:** `b183638b8e8d67d586af072434063faef8063648`
**GitHub Actions:** not dispatched (paid CI denied)
**IPA/device test:** not produced / not performed

## Outcome

**PARTIAL.** The existing region-import path and its limitations were revalidated, the importer was made reusable for region-specific generated C, and the Hyrule-enabled collision grid now covers the imported field and Castle connector without changing the ordinary build. The connected-world architecture and local build/device handoff are documented.

**The mission's continuous-world acceptance criterion is not met.** Castle Grounds still connects to Hyrule through paired fading warps. Kokiri is not registered, no cross-region transforms or merged collision/render data are integrated, and no iPhone build has been tested.

## Local implementation commits

- `e872ba16` — expand the opt-in collision grid for Hyrule coordinates.
- `afe0c691` — generalize OoT region import and record connected-world status.
- `daf0aac7` — add rigid yaw/translation transforms to generated region data.

These commits are local to branch `ios`; no push or paid workflow was started.

## Verified importer findings

From the local user-provided O2R archive:

- Hyrule Field (`spot00`): 37 display lists, 37 vertex arrays, 51 sampled textures, 1,639 rendered triangles, and 1,162 collision vertices / 1,579 collision triangles. Three segmented display-list calls remain unresolved. Collision bounds are X `[-11048,6732]`, Y `[-1095,1407]`, Z `[-1811,16529]`.
- Kokiri Forest (`spot04`): canonical root plus three default rooms emitted 40 Gfx lists, 34 vertex arrays, 60 texture resources, 2,060 render triangles, and 1,081 collision vertices / 1,692 collision triangles. Six segmented calls remain unresolved. The importer does not yet select and validate alternate scene setups or actor/object dependencies.
- The importer now supports region-specific symbol/output names, rigid Y-yaw/translation transforms, and display-list/vertex variants keyed by `(room mesh, polygon)`, so different room transforms cannot accidentally share a vertex array variant. The same baked transform reaches render vertices, collision, bounds, and selected spawns; tests cover the coordinate math and s16 checks. A draw-call summary bug that undercounted the Kokiri rooms was corrected. No real region layout is selected or integrated, and these source fixes do not establish complete Kokiri rendering.

See `HYRULE_IMPORTER.md` and `KOKIRI_FOREST_AUDIT.md` for the unresolved resource pointers and detailed decoding limits.

## Source changes

- `tools/hyrule_importer.py`, `tests/test_hyrule_importer.py`: generic region export, room-specific variants, world-transform baking for render/collision/spawn outputs, correct room call accounting, and deterministic importer regressions.
- `src/engine/surface_partition.h`, `surface_load.c/.h`, `surface_collision.c`, and `src/game/mario.c`: an opt-in 64-by-64 collision grid and larger pools for Hyrule-world builds; ordinary builds retain the stock 16-by-16 grid and pool sizes. `Makefile.split` now tracks the Hyrule build-mode stamp for the three objects whose compiled code embeds the partition configuration. `ios/test_surface_partition_bounds.py` checks coordinate coverage and this invalidation rule.
- `ios/build_cachyos_local.sh`: local preflight also converts and syntax checks the Kokiri default-scene subset, without registering it in runtime.
- Added `MISSION_55_3_BASELINE.md`, `CONNECTED_WORLD_ARCHITECTURE.md`, `LOCAL_BUILD_STRATEGY.md`, and this handoff/report.

## Validation

- **PASS:** `python3 tests/test_hyrule_importer.py` (31 tests).
- **PASS:** `python3 ios/test_surface_partition_bounds.py` (3 tests).
- **PASS:** `CC=clang tests/run_linux_checks.sh`.
- **PASS:** Hyrule and Kokiri importer output generated from the local O2R; both generated C includes passed host compiler syntax checks.
- **PASS:** A Hyrule conversion with 90-degree yaw and test translation produced transformed Gfx/collision/spawn output and passed host C syntax checking. That test transform is not a measured world layout.
- **PASS:** `./ios/build_cachyos_local.sh prepare /home/larry/Downloads/oot.o2r` completed, including asset verification, Linux regressions, packaging fixtures, and host object compilation.
- **PASS:** Hyrule surface-loader, collision, Mario integration, and level objects compiled with the host compiler in the enabled configuration.
- **PASS:** Reused build directory switched disabled → enabled; the surface loader, collision, and Mario objects rebuilt under both configurations.
- **BLOCKED:** Xcode/iOS SDK/Metal compilation and unsigned IPA packaging; the current Linux host has no Apple toolchain or iOS SDK.
- **NOT RUN:** GitHub Actions (no paid CI), iPhone installation, visual or physics device validation.

These host results do not prove iOS build compatibility, Metal rendering, seamless traversal, or complete Hyrule/Kokiri gameplay.

## Next milestone

First complete a private, Hyrule-enabled Apple-toolchain build and inspect Castle Grounds ↔ Hyrule on the iPhone. Then resolve Kokiri's six display-list dependencies and selected default setup. Only after authentic region meshes, collision, and entrances have been validated should the project choose their world transforms and combine them into one Castle Grounds area with a single SM64 `TERRAIN` path. Test collision seams, camera, memory, and player/vehicle state before claiming continuous-world functionality.
