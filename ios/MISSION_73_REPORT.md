# Mission 73 — Hyrule Field integration report

Date: 2026-11-19

## Status

**PARTIAL — authentic Hyrule Field now converts into WSC5 and passes a production host runtime probe. Apple Metal and iPhone rendering remain unverified.**

## Selected scene

Selected Hyrule Field (`spot00`, OoT NTSC 1.0) because the archive contains its room display lists, texture resources, lighting table, collision header, and a default entrance; it was the closest available scene to a complete conversion through the existing exporter and WSC room runtime.

The converter now carries an explicit source profile: frame 0, lighting row 8, daytime branch. It reconstructs the selected lighting and fog snapshot, resolves three supported dynamic segment calls, and records type-2 culling-sphere metadata while retaining the source polygon list order. It does not evaluate camera-dependent culling.

## End-to-end host result

From the user-supplied local archive, the converter produced one authentic Hyrule Field room as a WSC5 package:

- 37 room display lists, 3,735 commands, 844 candidate draws.
- 715 draws have fully interpreted state; 129 carry explicit rendering approximations; there are no unresolved draw issues in this selected daytime trace.
- 202 source-ordered batches, 57 unique materials, 1,639 render triangles, 50 decoded textures.
- 1,579 collision triangles and 14 retained safe entrances.
- Package size: 121,530 bytes. Local-only package SHA-256: `0b9c686560353c2f235dc8665d85c4439ae30691f7e9c00d943aa76322a56091`.

The package opened through the production C WSC provider. The host graphics recorder exercised texture upload and shader generation, a floor query succeeded at the selected spawn, and room teardown returned the region count to zero. The real-scene host view submitted 34 visible triangles, 12 draw calls, and 6 texture uploads. The probe's test projection does not establish full-scene visibility or final image correctness.

## Changes

- `tools/fast3d_pass_state.py`, `tools/o2r_draw_trace.py`: explicit Hyrule scene/pass context, room-mesh provenance, generated segment support and culling visibility diagnostics.
- `tools/o2r_to_wsc3.py`: Hyrule profile integration, explicit entrance exclusion and collision-flag omission policy, reportable approximations, WSC5 package output.
- `tools/world_scene_package.py`, `src/engine/world_scene_file.c`, `src/engine/world_scene_file.h`, `src/pc/gfx/gfx_pc.c`: WSC5 decal state, bounded 256 source-ordered batches, and WSC5-only 4,096-texel texture bound. WSC1–WSC4 keep their 64-batch and 2,048-texel limits.
- Regression tests cover source tracing/lowering, explicit approximation requirements, WSC5 batches and decal state, legacy limits, package load/reload/teardown, and an optional real WSC5 package host probe.
- `ios/WSC5_FORMAT.md` documents the added format rules.

## Concrete limitations

- The layout maps all 61 OoT collision surface definitions to SM64 default surface type. Their parameters are recorded but not translated. It also explicitly omits three source collision corner-flag categories. OoT's collision queries consume these vertex flags, so dropping them may change collision filtering; this conversion is not collision-semantic parity. The pinned OoT collision implementation checks these flags during surface queries: [z_bgcheck.c, NTSC 1.0 decomp](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_bgcheck.c).
- Four source entrances have no safe floor in the imported local collision and are excluded with report entries. Six water boxes are omitted, so water/swimming is unsupported.
- 129 CI draws are decoded using RGBA5551 despite source TLUT mode being disabled. Five coverage-alpha draws use native straight-alpha blending. CULLDL sphere visibility is not evaluated; all referenced polygon lists are retained.
- Lighting is a frame/lighting-row/daytime snapshot; nighttime dynamic state and camera-dependent translucent sorting are absent.
- Fifteen scene and room metadata types are not represented, including actors, object lists, alternate headers, exits, camera, skybox, audio, wind, paths, and special objects. This is a static environment subset, not a complete playable Zelda scene.

## Validation results

- **PASS:** `tests/run_linux_checks.sh`.
- **PASS:** `tests/test_world_scene_package.py`, `tests/test_o2r_to_wsc3.py`, `tests/test_o2r_draw_trace.py`, `tests/test_fast3d_pass_lowering.py`.
- **PASS:** WSC package suite and actual Hyrule package probe compiled with AddressSanitizer and UndefinedBehaviorSanitizer (leak checking disabled for the existing host harness).
- **PASS:** actual package load, supported host graphics replay, spawn floor query, and unload/cleanup.
- **PASS:** `git diff --check` and Python bytecode compilation.
- **BLOCKED:** Apple SDK and Metal validation: `xcodebuild` is unavailable locally. GitHub cannot currently be reached (DNS resolution failure) and `gh auth status` reports the stored token is invalid. No Actions workflow was dispatched, no remote commit was created, and no IPA was produced.
- **UNVERIFIED:** physical iPhone rendering, visual fidelity, full-scene camera coverage, collision behavior in gameplay, and frame performance.

## Asset handling

The source O2R archive and generated scene package/PNG textures remained local under `/home/larry/Downloads` and `/tmp`; none are included in this source change. The local package checksum above is for verification only and does not imply publication.

## Recommendation

After GitHub authentication/network access is restored, push this branch without rewriting history and run the existing manual Apple validation workflow. A full IPA is not yet justified: first validate Metal shader compilation, install the asset-free/runtime build, then deliver the external scene package through an authorized private path for a device rendering test.
