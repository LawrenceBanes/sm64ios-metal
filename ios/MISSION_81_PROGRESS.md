# Mission 81 progress

## Current checkpoint

- Branch: `mission-81-shader64-rendergraph`
- Base: `ae34240a53ba77eefe28676e9eb6a7f741e24aaf` (`mission-79-shader64`)
- Latest committed code checkpoint: `647e0842` (depth/material diagnostics); optional directional-light graph resource and Lumen64Graph are in the working tree for the next validation cycle.
- Status: first API 2 fullscreen DAG vertical slice committed, Apple-compiled, and packaged as a private unsigned device-test IPA; larger Iris-class stages remain incomplete.
- Physical-device output: not verified.

## Implemented in working tree

- Bounded API 2 graph validator and common per-pass Metal wrapper.
- iOS runtime pipeline preparation for each fullscreen pass, transactional graph activation, intermediate RGBA16Float targets, command-buffer completion lifetime, runtime target memory budget, and API 1 switching/Classic64 cleanup.
- iOS manifest handling for API 1 and API 2; API 2 startup restore reads original installed pass sources.
- Two-pass GraphDemo reference pack and Bliss camera-gridlines licensed adaptation pack.
- Apple Metal validation now compiles wrapped API 2 reference pass sources as well as the preserved API 1 packs.
- Added host checks for graph ordering, cycles, feedback, target limits, wrappers, pack import, and the API 2 examples.
- Architecture/resource contract and local Bliss archive compatibility map in `SHADER64_MISSION_81.md`.
- Fixed the GraphDemo `levels` range mismatch: the pack declares 2–16, while the runtime previously rejected/clamped values above 8. Float shader parameters now consistently allow a bounded ±16 across validation, persisted values, and Metal uniforms.
- Unified API 2 intermediate target sizing between host budget validation and Metal allocation using double-precision multiplication followed by floor-to-pixel sizing. A regression reproduces a 145-pixel dimension / `0x3ef3a431` scale rounding up to 69 under integer-to-float multiplication, where the encoded float's exact product is below 69 and must allocate 68.
- Production ZIP-importer coverage now packages and imports every checked-in reference pack (Classic64, Paper64, Lumen64, GraphDemo, BlissGridlines), verifies extracted source payloads, validates API signatures/graph structure, and checks declared float parameter bounds.
- Added an iOS Metal-only render-classification channel: GeoLayout master-list entries record world/object/Mario ownership, bounded tagged no-op commands carry the class through the Fast3D interpreter, and the Metal fragment parameter contract plus a Material Class diagnostic can observe it. The original combiner output remains unchanged.
- Added an optional API 2 post-combiner `shader64_material` function with bounded MSL source validation, pack parameter binding, material-class/UV/raster-position inputs, per-renderer shader regeneration, startup restore, and safe fallback to the native combiner on shader failure. This is a material fragment hook, not a world-space geometry or deferred shading API.
- GraphDemo now uses its fourth parameter to apply a Mario-only tint through the class-3 hook. The world and object classes return the untouched Fast3D combiner color. The production ZIP-importer test includes and validates the optional material source.
- Current working-tree targeted validation: `python3 ios/test_shader64_pack.py` (17 tests; includes production ZIP import of all reference packs and ASan/UBSan importer smoke), `tests/run_metal_shader_codegen_test.sh`, `tests/check_metal_project_wiring.sh`, and `bash -n ios/validate-metal-apple.sh` all pass. `CC=clang tests/run_linux_checks.sh` passes the complete regression suite; standard fixture warnings/invalid-input diagnostics remain.
- Fixed the earlier Apple compile error by removing the `gfx_pc.h` include from Objective-C Metal code (which exposed a host `Gfx` type unavailable in that translation unit) and using a narrow getter declaration instead.

## Tests so far

- `python3 ios/test_shader64_pack.py`: passed, 14 tests, including the float rounding boundary, the full reference-pack production-importer matrix, and ZIP smoke tests under ASan/UBSan.
- `tests/run_linux_checks.sh`: passed (complete suite, exit 0), including API 2 source wrapper and archive tests. Existing platform/action compiler warnings and fixture-invalid-scene diagnostics remain in suite output.
- `CC=clang tests/run_linux_checks.sh`: passed after adding the renderer material-class channel; this compiles the iOS/Metal Fast3D bridge and the graph-node tagging path with host stubs. It does not execute an iPhone GPU.
- Latest working-tree checks: `python3 ios/test_shader64_pack.py` (15 tests, passed including importer/ASan/UBSan ZIP checks), `tests/run_metal_shader_codegen_test.sh`, `tests/check_metal_project_wiring.sh`, and `CC=clang tests/run_linux_checks.sh` (all passed; complete suite, expected fixture diagnostics/compiler warnings only).
- `git diff --check`: passed at the previous checkpoint; rerun after remaining edits.
- Apple validation workflow `44787616391` passed both Linux and Xcode 27 iOS device/simulator compile/link jobs, including UIKit integration and wrapped API 2 GraphDemo/Bliss Metal sources. No GPU or iPhone runtime was executed.
- Apple validation workflow `44794122672` passed Linux regression and the Xcode 27 iOS device/simulator compile/link plus Metal source validation jobs for code checkpoint `227d1a4b2a8eaca73bddbe893c7b96081a69e276`.
- Apple validation workflow `44797419474` for `8f959e30` passed portable host checks but exposed a link-only gap in the standalone Metal backend validation executable: the new `gfx_current_render_material_class` symbol is implemented in the gameplay graphics bridge, which that small backend link intentionally omits. Added a class-0 stub only to the backend test executable; the workflow separately compiles the real `gfx_pc.c` implementation. This is test harness wiring, not a production fallback.
- Apple validation workflow `44797696601` passed both full Linux regression and Xcode 27 device/simulator backend compile/link plus wrapped MSL validation for `a6b1f8c382af35489b9223e30409ea181a7ee4a8`. No GPU execution or physical-device rendering occurred.
- Added a bounded optional API 2 `sceneDepth` pass input paired with the engine's inverse projection, gated by an explicit manifest boolean. `DepthDebug64` is a raw nonlinear-depth diagnostic example. The graph enables camera capture only while a pack requests the resource; single-sample depth/projection unavailability falls back to the native composite for that frame.
- Apple validation workflow `44799029668` passed both full Linux regression and Xcode 27 iOS device/simulator compilation plus API 1/API 2 GraphDemo, BlissGridlines, material-hook, and scene-depth MSL compile/link for `5d27783c002fa410f661376587c13381b8164750`.
- Added `NormalDebug64`, which reconstructs a view-space normal approximation from actual depth-neighbor samples, and `MaterialDebug64`, which colors actual world/object/character draws through the user-facing material hook. Apple validation now compiles all reference material and scene-depth sources rather than just GraphDemo's examples.
- Apple validation workflow `44799573736` passed Linux and Xcode 27 MSL/backend validation for `647e08427c82104d8766b35ae2446372816923b2`, including NormalDebug64 depth reconstruction and MaterialDebug64 classification MSL. No GPU run or device pixels were observed.
- Added an opt-in `sceneLighting` graph resource exposing the configured directional sun and inverse camera transforms alongside scene depth. Lumen64Graph demonstrates directional diffuse shading using a normal reconstructed from depth; external shadow-map access remains unavailable.
- Full IPA workflow `44787935139` passed Linux checks, Xcode 27 Metal validation, full iPhoneOS arm64 app build, IPA validation, and private release upload. Source SHA: `4442d743551c6f37169a04ddea01ff0a82121402`.
- Private draft release: https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-17f166d22fdc8261aac8
- IPA asset: `sm64ios-connected-world-arm64-unsigned.ipa`, 9,385,655 bytes, SHA-256 `e6c6731ae20708f24423fe8babf360acbefd07dc5ab3ee71f7e9959adf017e25`. Download: https://github.com/LawrenceBanes/sm64ios-private-builds/releases/download/untagged-17f166d22fdc8261aac8/sm64ios-connected-world-arm64-unsigned.ipa
- Verified embedded app identity: bundle ID `com.larry.shitboxdev`, display name `SHITBOX LABZ INC.`, version `0.69.420` (build `1`). The private-build repository is confirmed private.
- Apple compilation and packaging are verified by CI; Metal GPU execution and physical iPhone output are not verified.

## Remaining work / next

1. Run full Linux and Apple validation for the new `sceneLighting` contract and Lumen64Graph; fix shader or iOS compile failures and push a verified checkpoint.
2. Install a matching IPA on a test iPhone; import GraphDemo, DepthDebug64, NormalDebug64, MaterialDebug64, and Lumen64Graph. Visually verify class separation, depth, reconstructed normals, and lighting. No device output is confirmed yet.
3. Next reusable engine resources: expose built-in shadow maps and transforms, then textures/history/compute only after bounded contracts and renderer lifetimes are defined. Native vertex normals, G-buffers, external shadow-map sampling, external textures, and Bliss Minecraft uniforms remain unsupported.

Mission 81 is **PARTIAL**. API 1 preservation and host validation are necessary but do not establish GPU output, iPhone behavior, or Iris/OptiFine compatibility.
