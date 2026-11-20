# Mission 79 Progress

## Current checkpoint

- Branch: `mission-79-modern-metal-renderer`
- Base: Mission 76 `7c57539c85ccdd14734f83b994a894cbe197dc8e`
- Current source commit: `9d193d7fdaf4b1099e1f67a070162670fcbc57fb`, with the next fog-profile increment in progress.
- Mission 77 ray-tracing branch and Mission 78 worktree are independent and unchanged.

## Milestones

### M79-A — Infrastructure (partial)

- RGBA16Float offscreen scene targets on the Enhanced/offscreen path; BGRA8 direct Classic path retained.
- Float and drawable pipeline cache entries are disjoint.
- Internal Depth32Float target is shader-readable and stored for offscreen rendering.
- Fullscreen presentation samples scene color and composites to the drawable.
- CPU matrix inversion and row-vector depth reconstruction helpers have host tests.
- **Open:** scene HUD/UI is still included in the same color target; transparent draw ordering has not been validated; no device image comparison.

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
- Focused source/shader checks and postprocess ASan/UBSan pass. Full Linux regression and Apple SDK validation are running/pending for this follow-up commit.

### Build candidate status

- The private full-game IPA workflow [44696628601](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44696628601) succeeded for source `aff5bc593b9e5806995b2bf4dd3420180705e5e7`: iPhoneOS arm64 compile, unsigned IPA packaging/structure checks, and private draft upload all passed. Private draft release: [unsigned build 44696628601.1](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-e913a2ca7bce91e3e18f). IPA SHA-256: `44ed16d6fd278c336bfaa9b1445dd67ffa8e604e5e13a80bb15d7e7652daacaa`. This is an install candidate, not proof of effect appearance or device performance.

### M79-C — Directional lighting and shadows (not implemented)

- No shadow-depth pass, directional light, PCF filtering, or world-geometry receiver path exists yet.

### Remaining M79-D/M79-E/M79-F/M79-G/M79-H work

- Bloom, full color grading, adjustable fog and sky/god rays, unified visual presets, device performance measurements, successful full app build, IPA, and private release remain outstanding.

## Current validation

- Full local Linux regression suite passed after color-grade implementation. Previous backend-only Apple Metal validation and Linux checks passed at `a05012e9` in [run 44697022003](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44697022003); Apple validation for color grading is pending.
- Focused postprocess CPU math test passed ASan/UBSan.
- Apple SDK validation passed for both the HDR/depth foundation (`2be23bf8`, run `44690695136`) and SSAO (`31a60a53`, run `44693601221`).
- AO/depth debug-view code passed Apple and Linux jobs at `16887bb47ca4f3e791ee25fbdbd9ed879f462675`, workflow [44694078066](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44694078066).
- Tone-map/FXAA is committed as `2e1911824acd5791796c013979792b5c1c348971`; focused checks, the full Linux suite, and Apple SDK/MSL validation passed (workflow [44694655649](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44694655649)).
- No physical-device visual, AO quality, postprocessing quality, or performance claim exists. The available private IPA predates color grading and estimated-memory/effect-pixel counters.

## Next task

Commit and validate fog profiles, then build a current private IPA. Continue with AO controls and HUD isolation, real directional shadow mapping, improved sky/light shafts, and unified presets. Device screenshots and performance evidence remain required for acceptance.
