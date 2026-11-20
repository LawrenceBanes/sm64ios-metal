# Mission 79 — Rendering Modernization

## Branch and isolation

Work is isolated on `mission-79-modern-metal-renderer` at `/home/larry/Projects/sm64ios-mission79`, based on validated Mission 76 commit `7c57539c85ccdd14734f83b994a894cbe197dc8e`. Mission 77's ray-tracing branch and Mission 78's worktree are unchanged and are not ancestors of this branch. Mission 76 already contains the imported Hyrule runtime and draw-distance work.

## Initial render-path audit

- `gfx_pc.c` runs the Fast3D/RSP interpreter on the CPU. It transforms vertices through row-vector model-view/projection matrices and submits clip-space positions to the Metal renderer.
- `gfx_scene_capture` optionally records world/local triangle attributes, root view and projection matrices, and object transforms. It is disabled by default and can provide matrix provenance for effects without changing ordinary draw submission.
- `gfx_metal.m` owns a BGRA8 game color target, Depth32Float depth target, generated combiner pipelines, and an optional offscreen color resolve/presentation path. The depth texture currently has render-target usage only and the render pass discards depth at completion. Consequently it cannot yet feed a postprocess or SSAO pass.
- The existing full-screen pass samples only color. Enhanced mode reaches it through offscreen rendering; Classic 100%-scale 1x-MSAA can render directly to the drawable.
- Metal scene fragment shaders have no world/view-space position varying. A postprocess AO approach therefore needs correct depth retention and inverse-projection reconstruction. Any screen-space composite must also account for HUD/UI draws sharing the scene color target.

## Initial implementation checkpoint

Added tested CPU helpers for matrix inversion and RSP-convention depth unprojection. They reject singular/non-finite matrices and clear-depth samples. These helpers are an AO prerequisite only; they do not implement or claim SSAO.

## Required next gate: actual SSAO

1. Make a bounded offscreen render target mandatory only when an implemented effect requires it; leave Classic untouched.
2. Retain a sampleable Depth32Float target for single-sample SSAO and derive depth conventions from the renderer's RSP-to-Metal Z remap.
3. Add a Metal compute SSAO kernel using reconstructed view-space positions, depth-aware neighborhood tests, bounded samples, and edge-aware blur.
4. Apply AO only to scene geometry, preserving HUD overlays; validate this with controlled contact geometry and on-device AO-off/on screenshots.
5. Add disabled-by-default UI controls, memory/time counters, resize/lifecycle tests, Apple shader compile and device IPA.

Bloom, exposure, color grading, FXAA, fog, sky, and directional shadow maps remain future milestones. No visual effect, iOS build, IPA, or device behavior is claimed in this checkpoint.
