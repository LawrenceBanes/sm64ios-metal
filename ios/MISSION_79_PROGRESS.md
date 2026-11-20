# Mission 79 Progress

## Current checkpoint

- Branch: `mission-79-modern-metal-renderer`
- Base: Mission 76 `7c57539c85ccdd14734f83b994a894cbe197dc8e`
- Current source commit: `31a60a53ffb77410f1e81b3de3e03e100d634b79` (SSAO compute pass and UI toggle).
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
- Added source assertions for tone-map, exposure and FXAA shader paths. Focused shader and renderer wiring tests passed; full Linux and Apple validation are now being run for this increment.

### M79-C through M79-H — Not implemented

- Directional shadow maps, bloom/exposure/tone mapping/color grading/FXAA, modern fog/sky/god rays, unified visual presets, device performance measurements, full application build, IPA, and private release remain outstanding.

## Current validation

- Full local Linux suite and Apple workflow Linux suite passed for the current SSAO commit.
- Focused postprocess CPU math test passed ASan/UBSan.
- Apple SDK validation passed for both the HDR/depth foundation (`2be23bf8`, run `44690695136`) and SSAO (`31a60a53`, run `44693601221`).
- AO/depth debug-view code passed Apple and Linux jobs at `16887bb47ca4f3e791ee25fbdbd9ed879f462675`, workflow [44694078066](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44694078066).
- Tone-map/FXAA is committed as `2e1911824acd5791796c013979792b5c1c348971`; focused checks, the full Linux suite, and Apple SDK/MSL validation passed (workflow [44694655649](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44694655649)).
- No Mission 79 IPA, on-device rendering, or physical-device visual/performance claim exists.

## Next task

The next action is to produce a private device-test candidate from `2e191182` using the authorized IPA workflow while continuing implementation afterward. Then extend the postprocess chain with bloom and grading controls, AO controls/HUD separation, directional shadow mapping, atmosphere, and integrated presets. Device screenshots and visual/performance checks remain required for acceptance.
