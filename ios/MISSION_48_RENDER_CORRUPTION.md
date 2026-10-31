# Mission 48 — Render corruption findings

## Build 52.1 device images and `SM64Diagnostics(6).log`

The screenshots include a textured indoor Normal view, indoor UV/Tex ID/Flat/Triangles views, and outdoor Tex ID/Triangles views. Flat and Triangles keep Mario and the sampled level geometry broadly coherent; Tex ID shows distinct logical texture regions but cannot verify their pixel contents. UV shows continuous, repeated gradients, which is consistent with tiled coordinates and is not by itself evidence of bad normalization. The two 05:01:59 Flat images are byte-identical. There is no outdoor Normal screenshot in the supplied Downloads set, so the outdoor appearance cannot be compared visually against its diagnostic views.

The log does provide a matched outdoor texture draw. At `16:25:23.151`, source snapshot triangle `1078528` reports logical texture 182, a 32×64 RGBA16-origin tile, source UVs `(-0.76172, 0.98975)`, `(0.98145, 0.98975)`, `(0.98145, -0.33984)`, and sampler `(cms=2,cmt=2,linear=1)`. One millisecond later, Metal frame 4680 / draw batch 361728 reports the same logical texture, dimensions, shader, and UVs (within printed precision), but its actual sampler is `linear=0`, address `(2,2)`. Both sides therefore agree on the source-to-normalized UV conversion and clamp address modes for this draw; they disagree on filtering. The MSL receives those packed UVs directly. Its three-point filter matches the OpenGL reference formula, but it is enabled only when the backend's per-slot `sampler_linear` flag is true. The stale nearest state therefore also disables the three-point path when that filtering mode is selected.

This identifies a concrete state translation defect: `gfx_pc.c` stores sampler settings on each `TextureHashmapNode` and calls `set_sampler_parameters` only when that texture's cached settings change. OpenGL changes sampler parameters on the selected texture object. Metal instead owns one sampler object per tile slot. When texture A (linear) is selected, texture B changes that slot to nearest, and A is selected again, the shared graphics layer can correctly skip its setter because A's cached request is still linear. Before this correction, `gfx_metal_select_texture` rebound A's image but left B's nearest sampler active. The failure is intermittent because the result depends on which texture last changed the slot and whether the shared per-texture cache requests another update.

The Metal backend now stores the requested sampler state with each `MetalTexture`, restores it on texture selection when it differs from the current tile slot, and updates the slot only after a replacement `MTLSamplerState` is successfully created. Comparison uses effective repeat/mirror/clamp modes, matching the backend translation. Shutdown invalidates sampler-slot state. A host regression reproduces the logged linear-clamp → nearest-clamp → linear-clamp texture-switch sequence.

This fix is evidence-backed for the observed filtering mismatch; it does not explain malformed geometry or prove every texture artifact is resolved. The log does not contain an upload sample for texture 182 (upload sampling began at logical texture 204), so its complete decoded RGBA contents remain unverified. The smallest next device capture is an outdoor Normal screenshot paired with UV/Tex ID for the same camera and a targeted upload checksum/pixel sample for logical texture 182, after this sampler fix is installed. Device visual confirmation is still required.

Validation: Linux shader generation, Metal state, scene capture/BVH, texture-cache generation reset, and rendering-diagnostic source checks pass. Xcode 27.0.1 / iOS SDK 27.1 Metal Objective-C compilation and generated MSL validation passed in [run 41249982168](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/41249982168). The full arm64 IPA passed app build, package, and structure/dependency checks in [run 41249982145](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/41249982145), producing private [build 53.1](https://github.com/LawrenceBanes/sm64ios-metal/releases/tag/ios-unsigned-ipa-53-1), SHA-256 `7a875a915c8417e4206997f556cb6cb1496cb013fff2dd2fbc47dca88455a8b4`. This confirms compilation and packaging only. The iPhone still needs to show whether the filter correction improves the outdoor textures; it does not establish visual correctness or resolve any independent geometry/content problem.

## Build 47.1 device log

`SM64Diagnostics(4).log` confirms the simulation and Metal presentation path remain active during the reported visual corruption:

- Metal command buffers complete with status 4 and Core Animation reports positive `presentedTime` repeatedly after frame 2, through at least frame 7,080. Frame 1 alone reports time 0. This capture does not support a persistent presentation stall.
- The texture-cache **Tex log** switch is enabled at 13:32:14. No cache generation reset or failed active tile binding is logged. Metal upload checkpoints reach texture ID 256; the log has no later 384/512 checkpoint and does not show the 512-entry cache rollover. The Mission 47 rollover fix is therefore not implicated by this run.
- No Metal pipeline creation, MSL compilation, texture allocation, or GPU command failure appears in the capture.
- The existing drawable diagnostic labels repeated `CAMetalDrawable` object pointers as immediate reuse, but the log shows the underlying texture pointer alternating while the wrapper pointer repeats. Objective-C wrapper address reuse was being mistaken for drawable backing-resource reuse. Acquisition diagnostics now compare both identities and count only repeated backing texture pointers.
- The capture has no per-triangle combiner, source UV, packed vertex, or bound-resource snapshots, so it cannot establish where the first incorrect value appears.

## Build 52.1 diagnostic capture

`SM64Diagnostics(5) (2).log` spans 14:06:41–14:09:40 on iOS 27.0. It confirms the new diagnostic path is active:

- The log records 20 mode changes, including repeated cycles through Flat, UV, Tex ID, and Triangles. Sampled Metal draw snapshots carry each expected mode value (254 Normal, 139 Flat, 60 UV, 56 Tex ID, 79 Triangles). This proves the selected mode reached draw encoding; the log cannot record how the resulting image looked to the user.
- Game ticks remain at about 30 FPS. There are 96 sampled Core Animation checkpoints through frame 5,100; the cumulative displayed count is 5,097. The sampled checkpoint for frame 1 has `presentedTime=0`; the other 95 sampled checkpoints have positive presentation times. Drawable and backing-texture identities match at every sampled checkpoint.
- Cache occupancy reaches 300/512 and there are no generation resets. The capture reports no missing Metal texture/sampler, skipped triangle, shader/pipeline failure, texture creation failure, or GPU command error.
- Upload samples have `sourceRowPitch = width * 4` for the sampled RGBA8 textures. They prove the logged bytes and row pitch passed to Metal, but the first four pixels alone cannot verify a whole texture against its source.
- Logged UVs include negative and greater-than-one values. Those are valid with repeat/mirror addressing and can also be intentional tile coordinates; without the corresponding visual UV-mode result and exact surface/tile state, their range alone does not prove an error.

This capture narrows the problem away from a stalled game loop, persistent presentation failure, cache rollover, and reported Metal resource/pipeline errors. It still does not distinguish a texture-coordinate/binding issue from bad geometry because there is no screenshot or description of what the same Goomba/terrain patch looked like in each mode. No production renderer fix is justified by the log alone.

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

Linux shader-generation, renderer syntax, cache-reset, scene, state, CPU-BVH, diagnostics-mode, and presentation-source tests pass. Xcode 27.1 / iOS SDK 27.1 device and simulator builds, generated MSL compilation, the full arm64 unsigned IPA build, and package validation passed in [Apple validation run 41231848990](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/41231848990) and [IPA run 41231849003](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/41231849003). The private diagnostic package is [build 52.1](https://github.com/LawrenceBanes/sm64ios-metal/releases/tag/ios-unsigned-ipa-52-1), SHA-256 `ca2505eded431c9da90144324834f24d5622f1c89ac8d46ffc8306abfc2a02a8`.

The new screenshots provide visual results for Flat, UV, Tex ID, Triangles, and indoor Normal, while the log provides the matched outdoor sampler state described above. The outdoor Normal view is absent, and texture 182's upload bytes were not sampled. Thus a concrete filtering mismatch is fixed in source, but visual correctness and any separate geometry/content defects remain unverified until a replacement build is tested on-device.
