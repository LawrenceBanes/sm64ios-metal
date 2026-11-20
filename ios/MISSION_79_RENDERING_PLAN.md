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

Added tested CPU helpers for matrix inversion and RSP-convention depth unprojection. They reject singular/non-finite matrices and clear-depth samples. Offscreen Enhanced rendering now uses an RGBA16Float scene color target and format-distinct pipeline-cache slots, preserving the direct BGRA8 Classic path. The internal Depth32Float texture is shader-readable and stored at the end of offscreen passes; direct Classic rendering retains the previous discard action. The existing final presentation pass composites the float target into the drawable.

This is the 79A render-target foundation, not a complete postprocess pipeline. HUD/UI is still rasterized into the same scene color target and is therefore not yet isolated from postprocessing. No tone mapping, AO, bloom, fog, sky, shadows, or god rays are implemented in this checkpoint.

## M79-B in progress: SSAO

Added a native Metal compute path behind a persisted developer toggle. Enabling AO forces bounded offscreen rendering at 1x MSAA, enables bounded scene capture for the camera projection, reconstructs view-space positions from the retained depth texture, derives normals from neighboring depth, tests an eight-direction neighborhood against a hemisphere, masks clear-depth sky, applies a depth edge guard, and runs a 3x3 depth-weighted denoise pass at half resolution. The final Metal presentation shader multiplies scene color by the filtered AO texture. The Graphics panel exposes an AO On/Off control and reports processed pixels. Capture is capped at 8,192 triangles; the shader currently samples depth directly and does not need copied world geometry.

This code is not yet visually verified. The final AO modulation currently affects HUD pixels that overlap scene depth because SM64 HUD display lists share the scene color target. The requested world/HUD separation remains open. Radius/bias/intensity and quality settings are currently fixed shader parameters, and there is no AO-only debug view. Avoid calling the milestone complete until the Apple shader workflow succeeds and contact occlusion has been inspected on actual SM64 and WSC geometry.

## Remaining architecture gates

1. Separate or correctly mask HUD/UI before adding bloom, exposure, grading, or AO composition; the current SM64 renderer submits world and HUD through a common color pass.
2. Add controllable values, AO/depth debug view, edge/temporal stability validation, and physical AO-off/on captures.
3. Then implement rasterized shadow maps independently of Mission 77 ray tracing, followed by HDR postprocessing and atmosphere.

Bloom, exposure, color grading, FXAA, fog, sky, and directional shadow maps remain future milestones. No visual effect, iOS build, IPA, or device behavior is claimed in this checkpoint.
