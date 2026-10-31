# Mission 48 — Render corruption findings

## Build 47.1 device log

`SM64Diagnostics(4).log` confirms the simulation and Metal presentation path remain active during the reported visual corruption:

- Metal command buffers complete with status 4 and Core Animation reports positive `presentedTime` repeatedly after frame 2, through at least frame 7,080. Frame 1 alone reports time 0. This capture does not support a persistent presentation stall.
- The texture-cache **Tex log** switch is enabled at 13:32:14. No cache generation reset or failed active tile binding is logged. Metal upload checkpoints reach texture ID 256; the log has no later 384/512 checkpoint and does not show the 512-entry cache rollover. The Mission 47 rollover fix is therefore not implicated by this run.
- No Metal pipeline creation, MSL compilation, texture allocation, or GPU command failure appears in the capture.
- The existing drawable diagnostic labels repeated `CAMetalDrawable` object pointers as immediate reuse, but the log shows the underlying texture pointer alternating while the wrapper pointer repeats. Objective-C wrapper address reuse was being mistaken for drawable backing-resource reuse. Acquisition diagnostics now compare both identities and count only repeated backing texture pointers.
- The capture has no per-triangle combiner, source UV, packed vertex, or bound-resource snapshots, so it cannot establish where the first incorrect value appears.

## Backend comparison

| Stage | Existing OpenGL reference | Metal implementation | Finding |
|---|---|---|---|
| Display-list interpretation | `gfx_pc.c` | `gfx_pc.c` | Shared code selects combiners, imports N64 textures, transforms tile UVs, and packs vertices before either backend. No backend-specific mismatch is established here. |
| Vertex stream | Position, optional UV, optional fog, then combiner inputs in one interleaved stream | Same dense order and byte stride from `gfx_metal_vertex_layout.h` | The earlier sparse-attribute defect remains covered by tests. Current diagnostic snapshots now include actual attribute formats, offsets, stride, clip positions, and UVs. |
| Texture conversion/upload | CPU expansion to RGBA followed by `glTexImage2D(GL_RGBA, GL_UNSIGNED_BYTE)` | Same CPU RGBA bytes followed by `replaceRegion` on `RGBA8Unorm`, row pitch `width * 4` | The upload contract matches at the API boundary. No device-side pixel readback or representative corrupted texture bytes were present in the supplied log, so channel/content correctness is not yet proven. |
| UV and filtering | Shared CPU tile-origin subtraction, half-texel adjustment for non-point filtering, tile-size normalization; GLSL three-point formula | Same packed UV; MSL three-point formula matches the GLSL expression | No extra Metal UV normalization was found in source. Device UV visualization is added to test the values after interpolation. |
| Samplers and state | GL repeat/mirror/clamp, nearest/linear and combiner state | Metal repeat/mirror/clamp, nearest/linear, cached pipeline/depth/blend state | No state mismatch is proven. Device snapshots now report tile modes, sampler identity, texture dimensions, format, generation, and relevant depth/blend state. |

The source audit has not found a proven production rendering defect beyond the already-fixed cache-generation issue. It would be speculative to change texture conversion, UV scaling, or geometry packing based on this log alone.

## Device isolation modes

Open the native diagnostics panel and tap **Mode** to cycle through `Normal → Flat → UV → Tex ID → Triangles → Normal`. The selection is process-local and starts at Normal on each launch.

- **Flat** substitutes white for texture combiner inputs before any texture sample. It preserves geometry, vertex/primitive combiner inputs, fog, alpha state, depth, and blending. If a Goomba is still geometrically malformed, texture pixels are not the cause of that shape.
- **UV** displays `fract(interpolatedUV)` from the generated MSL fragment input. A regular gradient/tiling pattern supports the CPU mapping; seams, unexpected scale, or discontinuities point to upstream UV/tile data or interpolation.
- **Tex ID** assigns a stable color to each logical Metal texture ID, blending colors for dual-texture shaders. Unexpected color regions isolate binding/selection changes from pixel content.
- **Triangles** uses per-triangle barycentrics and fragment derivatives for software triangle edges; it does not rely on unsupported iOS wireframe mode.

Snapshots are opt-in: select a non-Normal mode or enable **Tex log**. CPU and Metal draw snapshots are capped to the first 16 samples and then one sample every 256 draws. They include combiner/shader IDs, source and Metal texture identities, texture size/format/generation, sampler modes, cache occupancy/high-water/rollovers, representative positions/UVs/colors, vertex layout, and render state. During enabled diagnostics, Metal also records the first four RGBA pixels and row pitch for the first 16 texture uploads, then every 128th upload. **Tex log** writes an explicit enabled/disabled line. These bytes show exactly what reaches Metal's texture upload call and can be checked against the source N64 decode.

## Validation and next experiment

Linux shader-generation, renderer syntax, cache-reset, scene, state, and CPU-BVH tests pass. Source checks cover mode selection, bounded snapshots, and the unchanged `CAMetalLayer.nextDrawable` path. Apple MSL compilation and an iPhone run are still required; Linux cannot compile MSL or UIKit/Metal Objective-C code.

On device, use one stable scene and inspect the same Goomba/terrain region in this order: **Flat**, **UV**, **Tex ID**, then **Triangles**. Share the diagnostics log after enabling **Tex log**. If Flat and Triangles have correct shapes but UV/Tex ID or Normal is wrong, focus next on texture/tile mapping and binding. If Flat remains malformed, compare packed positions and combiner inputs before changing the texture path. Compilation or IPA packaging alone will not establish visual correctness.
