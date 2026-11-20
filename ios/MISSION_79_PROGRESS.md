# Mission 79 Progress

## Current checkpoint

- Branch: `mission-79-modern-metal-renderer`
- Base: Mission 76 `7c57539c85ccdd14734f83b994a894cbe197dc8e`
- Current pushed source commit before the tone-mapping increment: `4b7f7d0f` (independent fog density and start distance); Apple Metal SDK and Linux CI passed in [run 44710368065](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44710368065). Full IPA build for `dd3d5894` is running in [44709981511](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44709981511).
- Full IPA workflow [44707824529](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44707824529) succeeded for source `6866de41`, including HUD isolation and shadow-map reuse; it predates the adjustable Bloom UI and following controls.
- Mission 77 ray-tracing branch and Mission 78 worktree are independent and unchanged.

## Latest M79-A checkpoint (`eda005d8`)

- Added an iOS/Metal display-list marker between `geo_process_root()` and HUD/text/menu drawing. `gfx_pc` flushes the 3D batch at that marker and calls an optional rendering API hook.
- In the offscreen path, Metal now encodes bloom/AO/light shafts/shadows and composites the world to the drawable before opening a drawable-sized transparent HUD pass with its own cleared depth target. The UI is therefore excluded from scene grading, fog, bloom, SSAO, shadowing, and sky replacement. If the overlay depth allocation fails, the renderer keeps the old single-pass composite path. Classic/direct rendering does not split.
- Focused source checks, generated MSL checks, `git diff --check`, and the full local Clang Linux regressions pass. Apple Metal/MSL and Linux validation passed for committed HUD-split code in [run 44707130346](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44707130346). A full IPA including the split is pending; physical visual verification remains pending.

## Latest M79-G checkpoint (`6866de41`)

- Added a portable deterministic cache key over transformed world-space caster vertices, directional light, and shadow resolution. The production shadow pass can reuse the prior depth map and fitted transform if that key and vertex count are unchanged; moving/dynamic captured objects, light changes, and map-size changes invalidate it.
- `tests/run_shadow_map_test.sh` passes; direct Clang ASan/UBSan execution of `tests/shadow_map_test.c` passes with leak detection disabled; renderer source checks and full local Clang Linux regressions pass. Apple validation passed in [run 44707673489](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44707673489).

## M79-D adjustable Bloom checkpoint (`e5e7e60e`)

- Bloom now has Low / Standard / High settings which map to real bright-pass thresholds and composite intensities, are persisted, and are adjustable from the existing Bloom control. Presets choose the profile, and Reset restores Standard. The Standard profile preserves the previous values.
- Portable profile behavior, focused UI/source checks, ASan/UBSan state tests, and full Clang Linux regressions pass. Apple validation is running.

## Current M79-F control fix

- The MSAA control now disables for every active mode that forces single-sample rendering (SSAO, shadows, screen-space light shafts, or reconstructed sunlight), matching the backend's actual target selection. Focused source checks and the full local Clang Linux regression suite pass; Apple validation passed in [run 44708618479](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44708618479).

## M79-D manual color controls (`d3c6860b`)

- Added bounded, finite-checked brightness, contrast, saturation, and temperature adjustments, composited with the existing grade presets and persisted independently. The color menu exposes each adjustment and a reset action; Classic/reset restore neutral values.
- Focused state/source tests, full Clang Linux regressions, and direct ASan/UBSan state tests pass. Apple SDK/MSL validation passed in [run 44709405325](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44709405325). Appearance is still unverified on physical hardware.

## M79-B individual AO tuning (`dd3d5894`)

- Added persistent, bounded radius, bias, and intensity adjustments layered on the Low / Standard / High sample profiles. The AO menu exposes profile selection, each parameter, and reset; presets and Classic restore neutral tuning.
- Focused state/source/shader checks, full Clang Linux regressions, and direct ASan/UBSan state tests pass. Apple Metal SDK validation and CI Linux suite passed in [run 44709773683](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44709773683). AO remains visually unverified on physical hardware.

## M79-E fog density and distance tuning (`4b7f7d0f`)

- Added bounded persistent fog density/start-distance multipliers layered on the four existing fog color/density/range profiles. The developer Fog control now exposes profile selection, near/far start, density adjustment, and reset; presets and Classic restore neutral multipliers.
- Focused state/source/shader checks, full Clang Linux regressions, and direct ASan/UBSan state tests pass. Apple Metal SDK and Linux CI passed in [run 44710368065](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44710368065). No physical appearance test has been performed.

## M79-D tone-map mode selection (working tree)

- Added persistent ACES-inspired and Reinhard curve selection in the existing final composite shader. The developer control retains an independent tone-mapping on/off action, displays the selected curve, and presets/reset choose an explicit mode.
- Focused state/source/shader checks, full Clang Linux regressions, and direct ASan/UBSan state tests pass. Apple validation and device image comparison are pending.

## Milestones

### M79-A — Infrastructure (partial)

- RGBA16Float offscreen scene targets on the Enhanced/offscreen path; BGRA8 direct Classic path retained.
- Float and drawable pipeline cache entries are disjoint.
- Internal Depth32Float target is shader-readable and stored for offscreen rendering.
- Fullscreen presentation samples scene color and composites to the drawable.
- CPU matrix inversion and row-vector depth reconstruction helpers have host tests.
- Native HUD/text/menu commands now have a post-composite overlay boundary in the offscreen path (latest code still needs Apple validation). **Open:** transparent world geometry ordering has not been validated; no device image comparison.

### M79-B — SSAO (implementation in progress, not accepted)

- Added native Metal compute source: half-resolution position reconstruction, local normal reconstruction, eight-direction geometric hemisphere tests, sky masking, depth discontinuity guard, and depth-weighted 3x3 denoising.
- Added persisted On/Off control in developer Graphics panel; AO forces single-sample offscreen rendering and caps scene capture at 8,192 triangles.
- The developer control now cycles through Off, applied AO, AO-only debug, and scene-depth debug; the final presentation shader exposes those debug views.
- Added AO pixel counter and host checks for generated shader source and capture limit.
- Focused host checks passed locally: `tests/run_metal_shader_codegen_test.sh`, `tests/run_scene_capture_test.sh`, `tests/run_gfx_postprocess_test.sh`, `python3 ios/test_metal_graphics_foundation.py`.
- Apple Metal SDK/iOS backend validation passed at commit `31a60a53ffb77410f1e81b3de3e03e100d634b79`, workflow [44693601221](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44693601221); production SSAO MSL compilation succeeded.
- **Open:** The debug views compile under Apple MSL, but world/HUD compositing can darken HUD; radius/bias/intensity/quality are fixed; no hardware render, screenshots, or physical device verification.

### M79-D — Postprocessing (first functional increment)

- Added an optional presentation-pass ACES-inspired tone-map curve with persisted exposure steps (-1/0/+1 EV), plus a separate FXAA toggle. Both request the offscreen path only when enabled, and the default/Classic path remains direct.
- Added in-panel tone-map, FXAA, and exposure controls. This is a first increment only: bloom, brightness/contrast/saturation/temperature grading, FXAA image-quality validation, and HUD isolation remain open.
- Added source assertions for tone-map, exposure and FXAA shader paths. Focused checks, the full Linux suite, and Apple SDK/MSL validation passed (workflow [44694655649](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44694655649)).

### M79-E — Atmosphere (first functional increment)

- Added an opt-in exponential distance-fog composite using the retained Depth32 texture and inverse captured projection; clear-depth sky is masked. Added a persisted Fog toggle in the developer graphics panel.
- Fog currently uses a fixed cool gray-blue color, density, and start distance. Sky gradients/presets, sun disk, screen-space light shafts, adjustable fog parameters, world-scale tuning, and device visual checks remain open.

### M79-D — Bloom (implementation increment)

- Added a quarter-resolution bright pass and two separable blur passes; presentation can composite bloom before tone mapping. A persisted developer toggle keeps it off by default.
- Apple Metal SDK/MSL validation passed at `aff5bc593b9e5806995b2bf4dd3420180705e5e7`, workflow [44696615662](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44696615662), including the bloom compute source and VERSION_US area syntax path. Visual output has not been verified.

### M79-D — Color-grade presets (implementation increment)

- Added persisted Neutral, Warm, Vivid, and Cinematic grade selections to the final Metal composite. The shader adjusts saturation, contrast, temperature bias, and brightness before optional tone mapping; non-neutral grades activate the offscreen path. Reset returns to Neutral.
- UI and shader-generation source checks pass. Full Linux regression, Apple SDK/MSL validation, and sanitizer checks passed for `9d193d7f`; Apple workflow [44697713975](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44697713975). Full IPA predates this increment. Visual effect still needs device observation.

### M79-E — Fog controls (implementation increment)

- Added four persisted fog profiles that alter exponential density, start distance, and fog tint; profile cycling and reset are wired into the Graphics panel.
- Focused source/shader checks, postprocess ASan/UBSan, and full Linux regression pass. Apple SDK/MSL validation passed in [run 44697954395](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44697954395). Fog profile appearance and scale tuning remain device-unverified.

### M79-B — SSAO quality controls (implementation increment)

- Added persisted Low / Standard / High AO profiles. They vary sample count (4/8/8), sample spacing, bias, intensity, and view-space radius; a developer control exposes the selection and reset returns to Standard.
- Source checks and shader-source generation checks pass; the full Linux suite passed locally. Apple SDK/MSL validation passed at `095517d4` in [run 44698561959](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44698561959). Quality selection has not been visually compared on hardware.

### M79-E — Depth-masked sky presets (implementation increment)

- Added persisted Original / Day / Sunset selection. The final presentation pass replaces only clear-depth pixels with a camera-relative horizon gradient, so scene geometry remains untouched and sky pixels stay excluded from fog and AO reconstruction.
- This is a simple gradient, not an OoT skybox or sun disk. Full Linux regression and Apple SDK/MSL validation passed for the combined AO/sky commit in [run 44698930438](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44698930438). Sky appearance remains device-unverified.

### M79-F — Unified visual presets (implementation increment)

- Expanded settings state to Classic / Enhanced / Cinematic / Performance and added base render configurations. The UIKit control applies these profiles to the implemented AO, tone mapping, FXAA, bloom, fog, color-grade, and sky features; reset restores Classic.
- Focused host checks, full Linux regression, and ASan/UBSan state/postprocess tests pass. Apple SDK/MSL validation passed for the preset change in [run 44699738540](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44699738540); profiles still require device usability/performance feedback.

### M79-F — Master effects switch (implementation increment)

- Added a persisted master switch that gates AO, tone mapping, FXAA, exposure, bloom, fog, color grading, and sky presentation without deleting the individual saved choices. Turning effects off clears the AO/bloom work counters; Classic preset/reset disable it.
- Focused source checks and full Linux regression pass. Apple SDK/MSL validation passed in [run 44700236497](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44700236497). Physical switch usability and resulting visual fallback remain unverified.

### M79-C — Directional shadow mapping (first integration increment)

- Added bounded CPU math for directional-light basis construction, stabilized light-space bounds, and world-point projection to shadow-map clip/UV/depth coordinates. XY projection now uses the stabilized square extent; regression tests include asymmetric bounds.
- Added a Depth32Float map, depth-only Metal pipeline, bounded conversion of up to 8,192 captured world-space triangles, map submission, and 3x3 PCF receiver sampling in final composition. The developer Graphics panel exposes Off/On and 512/1024/2048 resolution choices. Enhanced and Cinematic presets enable shadows at 512 and 1024 respectively; Classic/Performance keep them off. Shadow allocations and pipeline resources are released on shutdown/settings changes.
- The map currently uses a fixed directional vector and darkens the existing rasterized color based on the actual depth comparison. It is a first directional shadow pass, not a full physically lit sunlight model; actor/material-specific shadow exclusions, cascades, and dynamic/static caster separation remain open.
- Factored the engine's row-vector object-transform application into the portable shadow module and tested nonuniform scale, translation, and non-finite matrices.
- Focused shadow math, shader-emission/source checks, and full Linux regression pass. The Apple SDK/MSL compile and Linux jobs passed for the integrated depth pass in [run 44702337813](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44702337813) at `824385aed80c3a8e61224082e98ba2f0ad889514`. A direct Clang ASan/UBSan shadow math run also passed. Full app compile/package workflow [44702525374](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44702525374) is running for `9794d8bfd89be99ca57683069dc061a2c0211f6c`.

### Build candidate status

- The private full-game IPA workflow [44696628601](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44696628601) succeeded for source `aff5bc593b9e5806995b2bf4dd3420180705e5e7`: iPhoneOS arm64 compile, unsigned IPA packaging/structure checks, and private draft upload all passed. Private draft release: [unsigned build 44696628601.1](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-e913a2ca7bce91e3e18f). IPA SHA-256: `44ed16d6fd278c336bfaa9b1445dd67ffa8e604e5e13a80bb15d7e7652daacaa`. This is an install candidate, not proof of effect appearance or device performance.
- A newer full game build from source `3303360fa275b3b9d3726be51fa9a9c81a96e8e4` passed compile, IPA structure checks, and private upload in [workflow 44698163366](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44698163366). Private draft: [unsigned build 44698163366.1](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-f4ffbc277ed244358b57); IPA SHA-256 `df42a01c548d61b7f8369dc6c36079e0f54ccd35684edc5980bc499d2b8966a`. It contains color grading and fog profiles but predates the AO quality control and sky gradient.
- Full game build from source `300cc597685623222c2e96301ea801eeb8152cef` passed arm64 compilation, IPA structural validation, and private upload in [workflow 44699134859](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44699134859). Private draft: [unsigned build 44699134859.1](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-dff6475bfe493acf287f); IPA SHA-256 `7d5d98a0fa527fc508b53d8cc228b36904e89029cff53b4122ddded384fae472`. It includes AO quality and day/sunset horizon gradient, and predates four unified presets.

### M79-C — Remaining

- No physical scene image has confirmed visible/contact-correct shadows. Sun direction/intensity controls, more robust caster coverage than the bounded capture set, and large-world cascades remain open.

### Remaining M79-D/M79-E/M79-F/M79-G/M79-H work

- God rays, genuine sky/sun presentation beyond gradients, HUD/UI isolation, device performance measurements, screenshot-based visual acceptance, and final integrated build remain outstanding. Bloom, tone map/exposure, grade, fog, settings presets, and controls have source/host/Apple compile coverage but no physical visual verification.

## Current validation

- Full local Linux regression suite passed after color-grade implementation. Previous backend-only Apple Metal validation and Linux checks passed at `a05012e9` in [run 44697022003](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44697022003); Apple validation for color grading is pending.
- Focused postprocess CPU math test passed ASan/UBSan.
- Apple SDK validation passed for both the HDR/depth foundation (`2be23bf8`, run `44690695136`) and SSAO (`31a60a53`, run `44693601221`).
- AO/depth debug-view code passed Apple and Linux jobs at `16887bb47ca4f3e791ee25fbdbd9ed879f462675`, workflow [44694078066](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44694078066).
- Tone-map/FXAA is committed as `2e1911824acd5791796c013979792b5c1c348971`; focused checks, the full Linux suite, and Apple SDK/MSL validation passed (workflow [44694655649](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44694655649)).
- The latest completed full IPA predates the master switch and shadow integration. No physical-device visual, AO contact-occlusion, shadow, postprocessing quality, or performance claim exists.

## Latest shadow integration checkpoint (`9794d8bf`)

- Implemented a production-path depth-map pass from `gfx_scene_capture` and a final-pass receiver. The map's fitting, capture conversion, Metal depth writes, and PCF output are separate observable stages; triangle counts report processed bounded casters, not GPU frame-time measurement.
- `tests/run_shadow_map_test.sh`, `python3 ios/test_metal_graphics_foundation.py`, `tests/run_metal_shader_codegen_test.sh`, `tests/run_gfx_postprocess_test.sh`, and `git diff --check` passed. A direct Clang ASan/UBSan build/run of `tests/shadow_map_test.c` passed. The full `tests/run_linux_checks.sh` passed twice during this increment; the second used `CC=clang`.
- Apple backend/MSL validation passed at `824385ae` in run [44702337813](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44702337813); full app compile/package for `9794d8bf` passed in [44702525374](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44702525374). No physical iPhone/iPad output or performance data exists. Do not call shadows visually validated until the GPU output is inspected.

### M79-H — First shadow-enabled full app candidate

- Full iPhoneOS arm64 compile, unsigned IPA structure validation, and private upload passed in [workflow 44702525374](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44702525374), source `9794d8bfd89be99ca57683069dc061a2c0211f6c`. Private draft [unsigned build 44702525374.1](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-c8f3c97f60fd5b172288); IPA asset `sm64ios-connected-world-arm64-unsigned.ipa`, 9,333,214 bytes, SHA-256 `21499a2211e17a3c6d640d534c6fb29421fcd1fc1b887071a4515a886c8299ec`.
- The IPA contains the directional shadow-map implementation. It does not include the following screen-space light-shaft work. The build was not installed or visually tested on device.

### M79-E — Depth-occluded screen-space light shafts (implementation in progress)

- Added camera projection math for the fixed world-space light direction, a quarter-resolution depth-sampled radial kernel, final-pass additive compositing, persistent Off/On and Low/Standard/High controls, and an explicit UI label stating that this is a screen-space approximation rather than volumetric scattering.
- Focused source checks, shader generation, postprocess projection tests, and full Clang Linux regressions pass. The sun direction is projected from the same world-space direction as shadow casting; the shader samples clear-depth visibility along a radial screen-space ray and adds a warm shaft contribution at quarter resolution. This remains an approximation, not physically volumetric light.
- Apple MSL/backend validation passed for the light-shaft kernel in [run 44703856058](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44703856058), source `039bb3f991d63ac2cb4976c023af1b4e1c6c81ee`. This run fixed a real compile failure: Metal depth texture `read()` returns a scalar, not an RGBA value. Full app packaging for this source revision is still required. Physical output, sunlight visibility, and performance are unverified.
- Added a camera-projected sun disk on Day/Sunset sky presets. The sun direction is now shared by the light-shaft source and shadow caster and has persisted Afternoon/Morning/Noon/Sunset presets accessible from the sun/shadow control. This current preset-control change is not yet Apple-validated.
- `07728beb` Apple backend/MSL validation and Linux jobs passed in [run 44704150806](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44704150806).
- Optional sunlight intensity control (Off/Low/Medium/High) uses depth-neighbor reconstructed view normals transformed to world space, Lambert diffuse, and shadow-map PCF visibility. A demonstrated integration defect was fixed: inverse projection had only been computed when fog was active, leaving the shadow receiver's depth reconstruction matrix zero with shadows alone. Apple backend/MSL and Linux checks passed in run [44704797589](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44704797589).
- Graphics panel controls reflow against the current iPhone/iPad panel width rather than keeping the 520-point desktop layout. This change is included in `aa7c92c1`; focused and full Linux tests plus Apple backend/MSL validation passed, but it is not in the current IPA.
- The offscreen renderer now latches to direct drawable rendering after allocation/compositor failure and does not retry every frame. A deliberate graphics setting change clears the latch. The developer performance panel reports when enhanced effects are suspended on the direct fallback. This is in `12b4cfe8`; Apple backend/MSL and Linux checks pass in [run 44706252460](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44706252460).

## Next task

The latest full IPA is a private draft from source `12b4cfe8`, SHA-256 `bf00039f3eb18c046ad1a52cf889340de289ef9a46aaa791a7862e1bf447c725`, uploaded by workflow [44705973596](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44705973596), draft [unsigned build 44705973596.1](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/sm64-ios-unsigned-44705973596-1). It predates HUD isolation. Apple Metal/MSL + Linux validation for `eda005d8` passed in [run 44707130346](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44707130346).

Next: commit/push tone-map selection and run Apple validation; monitor IPA workflow `44709981511`, then prepare a newer full IPA once the new mode compiles. Continue remaining renderer controls and reliability items; physical-device image and timing review remains required.
