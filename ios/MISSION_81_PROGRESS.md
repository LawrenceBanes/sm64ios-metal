# Mission 81 progress

## Current checkpoint

- Branch: `mission-81-shader64-rendergraph`
- Base: `ae34240a53ba77eefe28676e9eb6a7f741e24aaf` (`mission-79-shader64`)
- Latest pushed checkpoint: `d7dafb22` (adds opt-in view-space vertex positions to API 2 material shaders; Apple workflow `44832579016` passed). Physical GPU output remains unverified.
- Status: first API 2 fullscreen DAG vertical slice committed, Apple-compiled, and packaged as a private unsigned device-test IPA; larger Iris-class stages remain incomplete.
- Physical-device output: not verified.

## Implemented in current branch

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
- Added API 2 `sceneShadows` resource contract and a Lumen64ShadowGraph reference pack. It requests the existing bounded native directional shadow-map generator, binds its depth texture and fitted world-to-shadow basis to a graph pass, and applies 3x3 PCF to depth-reconstructed world positions. The engine falls back to its native composite when the map or camera capture is unavailable. This is one map, with the native capture limited to 8192 source triangles; coverage, stability, and appearance remain unverified on a device.
- Added C wrapper and reference-pack importer tests for the shadow resource. `python3 ios/test_shader64_pack.py` passes 18 tests (including ASan/UBSan ZIP smoke); `CC=clang tests/run_linux_checks.sh` passed. `tests/run_metal_shader_codegen_test.sh`, `tests/check_metal_project_wiring.sh`, and `git diff --check` passed. Local `xcrun` is unavailable, so actual MSL compilation is pending Apple CI.
- Previous Apple workflow `44800637968` completed successfully for `fa481b40c5c0107161cca4ed469d575e495fee46`; it predates this shadow-map binding and does not validate the current working tree.
- Commit `885909b489cc747fb42623d8d147bc3c98608d36` was pushed. Apple validation workflow `44802320437` passed its full Linux regression job and Xcode 27 iOS device/simulator compile/link plus all wrapped MSL validation, including Lumen64ShadowGraph. This confirms compile/link only, not runtime GPU execution or visual correctness.
- Added bounded pack-owned PNG resources to API 2. ZIP import accepts direct `textures/*.png` files (4 MiB per file; 8 MiB whole pack); runtime checks dimensions (2048 maximum per side, 4 megapixels total), decodes to premultiplied RGBA8, binds up to four manifest-ordered slots on `packTextures` passes, and retains Metal textures through command completion. TextureDemo64 exercises the public contract. Texture passes are kept separate from scene-depth/light/shadow passes for this first binding version.
- Commit `d1199114b28f5cd563e5b9639a48172a1785a50f` was pushed after all 20 focused Shader64 tests, importer/sanitizer smoke tests, full `CC=clang tests/run_linux_checks.sh`, and wrapper generation passed. Apple workflow `44804504596` passed full Linux and Xcode 27 backend plus wrapped MSL validation, including TextureDemo64. No runtime GPU result is established.
- Paper64MaterialGraph is added in the current working tree: it quantizes the post-combiner Fast3D color and warms Mario independently using material classification. It does not use geometric normals or produce directional toon bands; importer tests pass and Apple MSL CI is pending.
- Current working change adds an opt-in `materialShader.packTextures` contract. It binds up to four already-bounded PNG resources to actual Fast3D material draws, with explicit texture count, generated MSL signature checks, and per-draw retains for safe resource lifetime. Paper64MaterialGraph now applies restrained paper-grain modulation through the character/world material hook. Focused tests (21), production ZIP importer, ASan/UBSan importer smoke, C shader codegen, project wiring, shell syntax, and the full `CC=clang tests/run_linux_checks.sh` pass. Apple validation for this working change has not run yet.
- User scope clarification: extensive public SDK, standalone validator product, tutorials, and community infrastructure are dropped. Keep internal API/manifest validation and minimal reference packs; continue rendering milestones and defer final main integration until achievable rendering work is complete.
- Integration ordering: a separate worktree was created from `origin/main` only to assess mergeability; its in-progress merge was aborted before changes were committed. It is clean at `bd755915`. No main or other mission worktree was modified. Integration must wait for the remaining achievable renderer work.
- Full IPA workflow `44787935139` passed Linux checks, Xcode 27 Metal validation, full iPhoneOS arm64 app build, IPA validation, and private release upload. Source SHA: `4442d743551c6f37169a04ddea01ff0a82121402`.
- Private draft release: https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-17f166d22fdc8261aac8
- IPA asset: `sm64ios-connected-world-arm64-unsigned.ipa`, 9,385,655 bytes, SHA-256 `e6c6731ae20708f24423fe8babf360acbefd07dc5ab3ee71f7e9959adf017e25`. Download: https://github.com/LawrenceBanes/sm64ios-private-builds/releases/download/untagged-17f166d22fdc8261aac8/sm64ios-connected-world-arm64-unsigned.ipa
- Verified embedded app identity: bundle ID `com.larry.shitboxdev`, display name `SHITBOX LABZ INC.`, version `0.69.420` (build `1`). The private-build repository is confirmed private.
- Apple compilation and packaging are verified by CI; Metal GPU execution and physical iPhone output are not verified.

## Remaining work / next

1. Expose useful actual Fast3D geometry attributes to the external material hook without changing the default combiner path; prioritize per-vertex normals if the native vertex layout supplies them consistently.
2. Device-test material classification, custom material textures, scene depth/normals, lighting/shadows, and reference packs against SM64 and WSC geometry. No GPU rendering or physical-device output is confirmed yet.
3. Native mesh normals, complete G-buffers, arbitrary shadow cameras/passes, multi-output compute, full geometry-stage overrides, and Bliss Minecraft uniforms remain unsupported.

## Current working increment — depth-derived scene normals

- API 2 graph passes may request the separate `sceneNormals` resource. A Metal compute pass reconstructs half-resolution view-space normals from current single-sample depth and inverse projection; sky and large depth discontinuities receive alpha 0. This is a bounded screen-space estimate, not native mesh normals.
- `NormalDebug64` reads the engine resource. iOS manifest parsing, shared graph validation, wrapper generation, and the production importer matrix recognize the contract. Binding-conflicting resource combinations are rejected.
- Fixed a shared-validator regression found during this work: resource bit 16 was missing from the accepted graph mask, causing the normal pack to be rejected even though UIKit parsing accepted it. Added explicit valid/conflict graph tests.
- `python3 ios/test_shader64_pack.py`: passed, 22 tests including ZIP importer and ASan/UBSan smoke. `tests/run_metal_shader_codegen_test.sh`, `tests/check_metal_project_wiring.sh`, `bash -n ios/validate-metal-apple.sh`, and `git diff --check`: passed.
- `CC=clang tests/run_linux_checks.sh`: passed the full suite. Existing fixture diagnostics and pre-existing compiler warnings remain.
- Commit `8a11e7315fe4a3f6b0d507cb3f0cbe10cfe99247` was pushed to `mission-81-shader64-rendergraph`. Apple workflow `44808512807` passed both full Linux regression and Xcode 27 iOS Metal backend/generated MSL validation, including the normal compute shader and wrapped NormalDebug64 pass. This does not execute a GPU or confirm device pixels.
- Linux tests cannot execute the iOS Metal compute pass or verify normal visualization.

## Current working increment — composable normal and texture inputs

- Extending API 2 so one fullscreen pass can sample both the reusable depth-derived `sceneNormals` texture and bounded pack-owned PNG textures. Normal stays at texture 1; four pack images stay at slots 4–7; texture count remains buffer 1; params remain buffer 0.
- Added a separate validated MSL contract and wrapper, enabled the compatible flag combination in shared/UIKit graph validation, and added `NormalTexture64` with a locally authored checker resource. Explicit scene-depth, lighting, and shadow combinations stay rejected due to current overlapping bindings.
- `python3 ios/test_shader64_pack.py`: passed, 23 tests including production import of NormalTexture64 and ASan/UBSan ZIP smoke. Shader codegen, project wiring, shell syntax, and `git diff --check` passed. Full `CC=clang tests/run_linux_checks.sh` passed.
- The first Apple run (workflow `44813432144`) exposed an extra closing parenthesis in Objective-C wrapper selection at `gfx_metal.m:1929`; host checks do not compile that file. The selection was rewritten as explicit branches. Apple retry `44813819600` passed both full Linux regression and Xcode 27 Metal backend/MSL compilation for `a444f2e2e4106abb2c7b465048c584a6257cf363`. No runtime GPU/device visual result is established.

## Current working increment — bounded API 2 compute nodes

- Added a fixed-contract Metal image compute node to API 2 render graphs. Compute nodes read their declared input at texture 0, write a bounded RGBA16Float intermediate at texture 1, receive pack parameters at buffer 0, and dispatch across graph-validated output dimensions. They cannot write `present`, request engine resources, or add texture/buffer bindings.
- Added ComputeBlur64, a two-node reference graph with a half-resolution 3×3 compute blur and fullscreen presentation pass. This exercises a pack-defined compute effect without a blur-specific engine path.
- Added restored-pack type validation and source/graph tests for valid contracts, bad resource slots, extra unbound textures, output-to-present rejection, and bounded graph behavior.
- Host validation before Apple CI: 25 focused Shader64 tests pass (including production importer and ASan/UBSan ZIP smoke); Metal codegen, Xcode project wiring, shell syntax, and `git diff --check` pass. `CC=clang tests/run_linux_checks.sh` completed with all suites passing; existing platform/action warnings and malformed-fixture diagnostics remain.
- Commit `0331d965263b75ec637965c09dacaa3cfa8b5705` was pushed to `mission-81-shader64-rendergraph`. Apple workflow [44815752341](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44815752341) passed the full Linux regression job and Xcode 27 iOS Metal backend/MSL compile/link, including ComputeBlur64. This is compile-time validation only: no GPU execution or device pixels were observed.
- Host checks do not execute the compute kernel. No GPU execution or device pixels are confirmed.

## Current working increment — user textures in compute nodes

- Extended the compute ABI with an opt-in `packTextures` binding: input/output remain at texture slots 0–1, up to four imported PNGs bind at slots 2–5, parameters remain buffer 0, and the declared texture count is buffer 1. The loader reuses existing PNG path, size, pixel-count, and ownership checks.
- Pipeline reflection validates all declared resource slots and access types before activation. Compute nodes may not request scene depth, normals, lighting, or shadows; they remain bounded by the existing graph pass and target limits.
- Added ComputeTexture64 using the existing locally authored checker PNG. Its kernel modulates the actual input image from a user texture and routes it through a generic fullscreen presenter.
- Focused Shader64 tests pass (26 tests), including production ZIP-importer checks and ASan/UBSan archive smoke. Metal source-generation, project wiring, shell syntax, and diff checks pass. Full `CC=clang tests/run_linux_checks.sh` passed with the usual fixture diagnostics and unrelated existing compiler warnings.
- Commit `dfcb0bbe` was pushed. Apple workflow [44816848250](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44816848250) passed the full Linux suite and Xcode 27 iOS Metal backend plus both compute MSL examples. The GPU kernels were compiled, not run; no device screenshot exists.
- The pack-texture compute increment is therefore host- and Apple-compiled, but not GPU/runtime-verified.

## Current working increment — frame context for fullscreen passes

- Added `frameInfo: true` for generic API 2 fullscreen passes, with `shader64_pass_frame` and a fixed binding at fragment buffer 8. The common float4 carries elapsed seconds, clamped frame delta, frame index, and output aspect ratio. This initial variant cannot be combined with the separately bound scene or pack texture interfaces.
- Added AnimatedGraph64, a source-defined time-varying color pulse, plus C validator/wrapper tests and graph resource-combination tests.
- Focused Shader64 validation passes (27 tests, including production importer and ASan/UBSan archive smoke), as do Metal codegen, Xcode project wiring, shell syntax, diff check, and full `CC=clang tests/run_linux_checks.sh`.
- Commit `288973e3b4832e87218df4b7b4f9398fc19e213e` passed Apple workflow [44818900682](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44818900682), including Xcode 27 iOS Metal backend and AnimatedGraph64 wrapper MSL compilation plus Linux regressions. This validates compile/link only; no runtime GPU result or screenshot is available.
- Next: extend compute scene data while preserving a low-overhead path. No final `main` merge yet.

## Current working increment — scene depth in compute passes

- Added optional `sceneDepth: true` for API 2 compute nodes. The engine binds its real single-sample Metal depth texture at texture/sampler slot 6 and inverse projection at buffer 3, while preserving image input/output slots 0–1, pack texture slots 2–5, frame context buffer 2, and pack texture count buffer 1.
- The production graph gates depth-dependent dispatch on the existing camera/depth availability checks. Shared validation rejects unsupported lighting, shadows, normals, resource combinations, and out-of-contract bindings. GPU pipeline reflection checks the depth texture, sampler, and inverse-projection buffer.
- Added DepthCompute64, which reconstructs view-space distance from scene depth and applies a depth fog pass, plus direct Apple MSL compilation coverage. This is source-level evidence of a real scene-data interface; it is not runtime visual proof.
- Focused pack tests (28), production importer matrix, ASan/UBSan ZIP smoke, codegen, Xcode wiring, shell syntax, and diff checks pass. Full `CC=clang tests/run_linux_checks.sh` passed with existing fixture diagnostics and compiler warnings only.
- Commit `f2f043ce7da92fc3b4aabc80881771efcb8299be` passed Apple workflow [44820115264](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44820115264), including the full Linux regression job and Xcode 27 backend/MSL compilation of DepthCompute64. This confirms compile/link only; it does not execute the depth kernel or verify fog pixels.
- No final `main` merge; GPU execution and physical-device image remain unverified.

## Current working increment — scene normals in compute and imported node dispatch

- Added `sceneNormals:true` for API 2 compute nodes at texture/sampler slot 7, alongside scene depth at slot 6 and bounded PNG inputs at slots 2–5. The source contract and Metal pipeline reflection validate the added read-only texture and sampler; the renderer reuses the existing half-resolution normals generated from current depth.
- Added `DepthNormalsCompute64`, which samples the real scene-depth and depth-derived normal resources in a generic compute kernel and forwards its result through the common fullscreen presenter. This is MSL/source and host-contract evidence only; no GPU execution or visual result is established.
- Fixed a production iOS pack activation defect discovered during the trace: `activateShaderPack` had hard-coded every API 2 pass as fullscreen, so imported compute passes could not reach their compute pipeline. The manifest `type` now maps to the corresponding bounded graph node type, with a regression assertion.
- `python3 ios/test_shader64_pack.py`: passed, 29 tests including the new combined-resource contract, reference-pack import, and ASan/UBSan ZIP smoke. `CC=clang tests/run_linux_checks.sh` passed. Metal codegen, Xcode wiring, shell syntax, and diff checks passed before the final documentation refresh; rerun diff checks before commit.
- Apple workflow `44821966525` passed Linux regression and Xcode 27 device/simulator Metal backend compile/link plus MSL compile/link for `DepthNormalsCompute64`. This is compile-time validation only: no GPU kernel execution, visual test, or device rendering. No final `main` merge.

## Current working increment — combined compute scene data and pack texture

- Added `SceneDataCompute64`, an API 2 reference graph pass that consumes its input image, one imported PNG (from the bounded four-texture slots), single-sample scene depth/inverse projection, and the engine's depth-derived view-space normals in the same kernel. Resource declarations use independent fixed Metal binding namespaces and are checked by the production source validator and pipeline reflection.
- Added the reference pack to the production-importer matrix and Apple MSL compile/link script. The checker texture is the existing locally authored fixture. Linux MSL syntax is not available; waiting for Apple validation.
- Focused Shader64 tests pass (29), including ASan/UBSan ZIP smoke and the full reference-pack import matrix. `CC=clang tests/run_linux_checks.sh`, Metal codegen, project wiring, shell syntax, and `git diff --check` all pass. No GPU execution or device output is claimed.

- Apple workflow `44822697079` passed both Linux regressions and Xcode 27 backend/MSL compile-link, including the combined texture/depth/normal compute kernel. This verifies the ABI source compiles and links but does not execute it on a GPU. No final `main` merge.

## Current working increment — directional lighting for compute nodes

- Added optional API 2 `sceneLighting:true` compute input. It binds the configured world-space sun direction/intensity at buffer 5 and inverse-view transform at buffer 4. Validation requires `sceneDepth:true` and `sceneNormals:true` with lighting, and the runtime uses the existing captured camera and sun settings rather than fabricating scene data.
- Extended `SceneDataCompute64` to use the reconstructed surface normal transformed into world space against the real configured sun, while retaining depth fog and the pack-owned texture. The pass remains an illustrative screen-space shader; it is not physical-device verified.
- Focused pack tests pass (29), including the reference pack with `sceneLighting`, graph-resource rejection for lighting without both depth and normals, and ASan/UBSan ZIP smoke. Full `CC=clang tests/run_linux_checks.sh`, Metal codegen, Xcode wiring, shell syntax, and diff checks pass; familiar fixture warnings/invalid-input messages remain. No GPU execution or device output is claimed.
- Apple workflow `44823825399` passed Linux regressions and Xcode 27 iOS Metal backend/MSL compile-link for the directional-light compute binding and `SceneDataCompute64`. This remains compile evidence only, not runtime shading or device output.

## Current working increment — native shadow-map input for compute passes

- Added optional `sceneShadows:true` to API 2 compute passes, with an explicit resource contract: native depth map and sampler at texture/sampler 8; the fitted world-to-shadow right/up/forward basis and ranges/bias at buffers 6–9. It requires scene depth, normals, and configured directional lighting, keeping all current binding namespaces disjoint.
- Reused the renderer's existing bounded directional shadow map and transform; the compute graph declines the custom path and uses the native composite when the shadow texture/camera data is unavailable. This does not add user-defined shadow cameras or increase capture coverage.
- Extended source and graph validation, UIKit manifest validation, Metal pipeline reflection, runtime bindings, production-importer tests, and `SceneDataCompute64`. The reference kernel reconstructs world position and applies a 3×3 depth comparison filter. Device appearance and GPU execution remain unverified.
- `python3 ios/test_shader64_pack.py` passes 29 tests, including production import of every reference pack and the ASan/UBSan ZIP smoke. A first full Linux run found a test assertion spelling mismatch; after correcting the assertion, the full `CC=clang tests/run_linux_checks.sh` passed. Metal codegen, project wiring, shell syntax, and diff checks pass.
- Commit `ca195ffe8285ea6e60a596968009f3f55bf8866d` was pushed to the isolated branch. Apple validation workflow [44825394576](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44825394576) passed the full Linux job and Xcode 27 iOS Metal backend/MSL compile-link, including SceneDataCompute64's compute shadow kernel.
- CI establishes source compile/link and host contract coverage only. The local Linux environment cannot execute Metal; shadow coverage and final pixels remain unverified on hardware.

Mission 81 is **PARTIAL**. API 1 preservation and host validation are necessary but do not establish GPU output, iPhone behavior, or Iris/OptiFine compatibility.

## Current working increment — opt-in native material vertex normals

- Added the API 2 material manifest option `materialShader.vertexNormals:true`. Activated packs receive `float3 viewNormal, bool normalValid` after `materialClass`; the source SM64 signed normal payload is transformed to normalized view space with the modelview inverse-transpose. Unlit or singular cases are explicitly invalid, so authors can preserve the original combiner output.
- Kept the default Fast3D path unchanged. An atomic activation flag avoids locks in the per-vertex path, and normals are transformed only for an active opt-in pack. The bounded VBO accommodates the optional float4 attribute. WSC normals are usable only when that source geometry carries a valid lighting-normal payload.
- Added the `VertexNormal64` host reference pack, production-importer coverage, material source validation, generated MSL ABI checks, normal-transform tests (identity, non-uniform scale, singular transform, zero normal), and Apple wrapper/compile wiring. This is not a physical rendering claim.
- Validation: `python3 ios/test_shader64_pack.py` passes 31 tests; `CC=clang tests/run_linux_checks.sh` passes the full suite (existing fixture diagnostics and known compiler warnings remain); Metal codegen, project wiring, shell syntax, and `git diff --check` pass. Apple MSL/backend validation and physical-device output are pending.
- Commit `12af2544` was pushed on `mission-81-shader64-rendergraph`. Apple validation workflow [44828619319](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44828619319) passed Linux regression and Xcode 27 Metal backend/MSL compilation, including VertexNormal64. This verifies compile/link only, not GPU execution or device pixels.
- Next: commit and push the directional-material-light checkpoint, validate on Apple CI, then continue toward external per-material lighting and the remaining renderer capabilities. No merge to `main` is planned at this checkpoint.

## Current working increment — directional light for normal-aware materials

- Added API 2 `materialShader.directionalLight:true`, valid only with `vertexNormals:true`. The hook receives normalized `sunDirectionView` and `sunIntensity` in the same camera basis as its source normals; intensity uses the existing sun setting. Added SunlitMaterial64 as a simple externally authored diffuse material example.
- Extended the existing fragment parameter block rather than adding per-draw buffers. Direction is transformed from the configured world-space sun using the captured camera view rotation. Split matrix capture from geometry capture so this material-only path obtains camera matrices without copying the renderer's per-triangle geometry; geometry capture remains enabled for shadow consumers.
- Added host validation for option dependencies, ABI generation, direction/normal transforms, production pack import, and matrix-only capture. Full `CC=clang tests/run_linux_checks.sh`, focused pack/codegen/scene-capture tests, project wiring, shell syntax, and diff checks pass. Targeted ASan/UBSan scene-capture and shader-codegen runs pass. Apple MSL/backend validation and physical-device output are pending.
- Commit `64e4df5c` was pushed to `mission-81-shader64-rendergraph`. Apple validation workflow [44830456738](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44830456738) passed the full Linux suite and Xcode 27 Metal backend/MSL compilation, including the SunlitMaterial64 wrapper. This does not execute shaders on a GPU or establish visual correctness.

## Current working increment — view-space position for material shaders

- Added the opt-in API 2 manifest property `materialShader.vertexPositions:true`. Material shaders receive a real view-space `float3 viewPosition` and `bool positionValid`, generated from Fast3D vertex coordinates and the active row-vector modelview transform. Position is explicitly invalid for singular/non-finite transforms; it is not inferred from clip-space coordinates.
- The interface is independently selectable from normals and directional light. Generated MSL, the packed vertex descriptor, Fast3D VBO data, UIKit manifest validation, API 2 restore, Classic fallback, and production ZIP import now share the flag. WSC geometry goes through the same Fast3D vertex loader and keeps the same view-space convention.
- Added `SurfacePosition64`, a minimal source pack that applies a position-dependent color modulation, plus generated MSL compile/link coverage for position, normal, and sunlit material variants. The default renderer and existing API 1 packs remain on their previous path.
- Validation: 31 focused Shader64 tests pass, including production import of all reference packs; `CC=clang tests/run_linux_checks.sh` passes the complete Linux suite. `tests/run_metal_shader_codegen_test.sh`, `tests/run_scene_capture_test.sh`, `tests/check_metal_project_wiring.sh`, `bash -n ios/validate-metal-apple.sh`, and `git diff --check` pass. Targeted ASan/UBSan runs for shader code generation and scene capture pass.
- Apple workflow [44832579016](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44832579016) passed both the complete Linux regression job and Xcode 27 iOS Metal backend plus generated MSL compile/link, including SurfacePosition64 and generated Fast3D variants for SurfacePosition64, VertexNormal64, and SunlitMaterial64. This is compile/link validation, not GPU execution or visual proof. No physical-device behavior is claimed; next: continue with the next useful renderer resource interface without merging to `main`.

## Current working increment — derivative-derived geometric normals example

- Added GeometryNormal64 as an API 2 reference pack that uses actual interpolated view-space positions and Metal fragment derivatives to derive and visualize per-triangle face normals. The effect is authored entirely in the pack shader; it does not add engine-side normal fabrication or change original Fast3D lighting.
- The result is a flat derivative normal, not a smooth vertex normal; discontinuities, triangle orientation, invalid transforms, or degenerate screen-space triangles can affect the output. Invalid data preserves the original combiner color.
- Added the pack to the production-importer matrix and Apple generated-Fast3D MSL compile/link workload. Runtime visual testing is pending.

## Current working increment — directional lighting from derived face normals

- Relaxed the API 2 `directionalLight` dependency to accept either source normals or actual view-space positions. The source must still declare and use the bounded sun-direction/intensity signature; position-only packs cannot claim source normals implicitly.
- GeometryNormal64 now derives a flat face normal from Metal fragment derivatives and applies the renderer's configured sun using an explicitly two-sided diffuse model. This avoids assuming the material hook's front/back winding semantics. The normal derivation and lighting are authored in the shader pack, not special-cased in the engine.
- Updated source validation, UIKit manifest checks, code generation, generated-MSL Apple coverage, and tests for valid position-based lighting and rejection when neither normal nor position inputs are declared. `python3 ios/test_shader64_pack.py`, `CC=clang tests/run_metal_shader_codegen_test.sh`, explicit position-lit MSL code generation, and the full `CC=clang tests/run_linux_checks.sh` pass. Apple validation is pending for this increment.
