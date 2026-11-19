# Mission 73 progress

Date: 2026-11-19

## Repository checkpoint

- Worktree: `~/Projects/sm64ios-mission72`
- Branch: `mission-73-hyrule-integration`
- Base: `ec90fea2fe7423be5f7d102819faa680e65fc438` (Mission 72)
- Current work is source and test changes only. The user O2R archive, generated WSC package, and extracted textures remain outside Git.

## Completed

- Added an explicit OoT NTSC 1.0 Hyrule Field trace profile, including source lighting-row selection and a deterministic daytime room-pass snapshot.
- Traced room-mesh type 2 as source-ordered polygon display lists while retaining culling spheres as metadata. Camera-dependent sphere visibility is reported, not guessed.
- Resolved three supported runtime-generated segment calls for the selected Hyrule snapshot; the daytime branch is required.
- Added WSC5 material decal/blend state and preserved source submission pass order. Added bounded 256-batch support only for WSC5; WSC1–WSC4 remain at 64 batches. WSC1–WSC4 texture limits remain 2,048 texels; WSC5 supports 4,096.
- Added explicit collision-flag and unsafe-entrance layout opt-ins with report entries for every omitted or approximated source behavior.
- Converted actual `spot00` Hyrule Field source resources into a private local WSC5 package and replayed it through the production C package reader, SM64 host graphics recorder/shader generator, and collision runtime.

## Actual source conversion result

Input: user-supplied local `/home/larry/Downloads/oot.o2r`, claimed revision NTSC 1.0 (`EC7011B7`). The converter validates its revision marker. Source assets and generated resources are not committed.

- Scene: `scenes/shared/spot00_scene/spot00_scene`; one default room.
- 37 display lists; 3,735 commands; 844 candidate draws: 715 complete, 129 approximate; no unresolved draw issues.
- Three generated segment calls were resolved under explicit Hyrule frame 0 / lighting row 8 / daytime assumptions.
- 202 source-ordered WSC5 material batches, 57 unique materials, 1,639 render triangles, 50 decoded textures, 121,530-byte package.
- 1,579 collision triangles; 14 safe entrances retained. Source entrances 2, 3, 7 and 14 had no safe local floor and were excluded only by explicit layout configuration and recorded in the conversion report.
- The output is in `/tmp/mission73-hyrule-export-final-20261119` for this session. SHA-256 of `scenes/4100.wsc`: `0b9c686560353c2f235dc8665d85c4439ae30691f7e9c00d943aa76322a56091`.

## Validation so far

- Full `tests/run_linux_checks.sh`: PASS.
- Focused WSC package, O2R exporter, draw trace, and pass-lowering tests: PASS.
- WSC package suite under AddressSanitizer and UndefinedBehaviorSanitizer: PASS.
- The real WSC5 Hyrule package loaded in the C runtime, produced a supported host Gfx/shader replay, returned a floor at its selected spawn, and unloaded without leaving a region. Sanitized replay: PASS. The bounded test view submitted 34 visible triangles across 12 draw calls and 6 texture uploads; this is not a full-scene visibility or Metal result.
- Two independent exports from the same local O2R and explicit layout produced byte-identical WSC5 packages (SHA-256 `0b9c686560353c2f235dc8665d85c4439ae30691f7e9c00d943aa76322a56091`).
- `git diff --check`: PASS.
- Apple/Xcode/Metal compilation: not run. This Linux host has no `xcodebuild`; GitHub API access currently fails with DNS resolution errors and `gh` reports the stored token is invalid.

## Remaining limitations

- This package is a narrow playable-geometry candidate, not a faithful full Hyrule port. All 61 source collision surface definitions map to SM64 default surface type; source surface parameters are reported but not translated. Three source corner-flag categories are explicitly discarded by layout opt-in. OoT uses these flags in collision query filtering, so their behavior is not preserved.
- Six source water boxes are omitted; water behavior is absent.
- Five rendering approximations remain, including 129 CI draws decoded as RGBA5551 without TLUT parity, five coverage-alpha draws lowered to straight alpha, and skipped type-2 visibility tests. The trace uses a daytime lighting snapshot; nighttime is not implemented.
- Fifteen scene/room metadata features (including actors, objects, alternate headers, scene exits, camera, skybox, path, sound, and special objects) have no runtime mapping.
- The real package has only passed a host runtime/render/collision probe. Apple Metal compilation and actual iPhone appearance are unverified.

## Next actions

1. Finish local review and rerun the suite after any edits.
2. Commit source/tests/docs on this branch without O2R-derived files.
3. Retry a normal push and manual Apple validation when GitHub connectivity/authentication is available. Do not force-push.
4. Do not claim iPhone readiness until Metal build and device rendering have been checked.
