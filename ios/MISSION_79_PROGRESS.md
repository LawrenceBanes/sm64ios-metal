# Mission 79 Progress

## Current checkpoint

- Branch: `mission-79-modern-metal-renderer`
- Base: Mission 76 `7c57539c85ccdd14734f83b994a894cbe197dc8e`
- Current source commit: `2be23bf8b67e3c814ad8e49b5def1743a8e18486` plus uncommitted SSAO work (checkpoint commit pending).
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
- Added AO pixel counter and host checks for generated shader source and capture limit.
- Focused host checks passed locally: `tests/run_metal_shader_codegen_test.sh`, `tests/run_scene_capture_test.sh`, `tests/run_gfx_postprocess_test.sh`, `python3 ios/test_metal_graphics_foundation.py`.
- **Open:** AO shader has not yet passed Apple Metal compilation in CI; world/HUD compositing can darken HUD; no AO-only debug view, tunable radius/bias/intensity/quality, hardware render, screenshots, or physical device verification.

### M79-C through M79-H — Not implemented

- Directional shadow maps, bloom/exposure/tone mapping/color grading/FXAA, modern fog/sky/god rays, unified visual presets, device performance measurements, full application build, IPA, and private release remain outstanding.

## Current validation

- Full local Linux suite passed after the RGBA16Float/depth foundation. The current uncommitted SSAO iteration has passed focused host tests; another full run is active before commit.
- Focused postprocess CPU math test passed ASan/UBSan.
- Apple SDK validation last passed commit `2be23bf8b67e3c814ad8e49b5def1743a8e18486` (Actions run `44690695136`); the current SSAO changes still need Apple compilation.
- No Mission 79 IPA, on-device rendering, or physical-device visual/performance claim exists.

## Next task

Run the full Linux suite to completion, commit/push the current SSAO implementation, dispatch Apple Metal SDK validation to compile the production AO and denoise MSL, and repair any Apple compiler/API failures. Then add bilateral upsampling/HUD isolation, tunable controls and debug views before visual AO validation. Continue to M79-C onward without treating a green workflow as mission completion.
