# Native iOS Metal port plan

## Selected base

Use `ckosmic/sm64ex-ios` as the working base. It is a direct `sm64ex` fork with an existing iOS application target, UIKit touch controls, SDL2 game-controller input, an iOS frame loop, and landscape-only iPhone orientation settings. It keeps the SM64 PC port's game and display-list code close to upstream.

Keep `DarioSamo/sm64rt` and `DarioSamo/sm64rt-legacy-renderer` as read-only references. Do not bring their Windows/DXR backend or DLL into the iOS build.

`sm64coopdx-ios` is a useful comparison for a newer CMake and IPA packaging path, but its much larger gameplay, mod, and network fork is not the right base for this port.

## Existing graphics seam

The game decodes Fast3D display lists in `src/pc/gfx/gfx_pc.c` and calls the renderer through `struct GfxRenderingAPI` in `src/pc/gfx/gfx_rendering_api.h`. The interface already exposes shader IDs, texture uploads, sampler modes, depth state, viewport/scissor state, and triangle batches. The current OpenGL backend is in `src/pc/gfx/gfx_opengl.c`; it generates shader source from the N64 combiner IDs. The iOS target currently selects that OpenGL ES backend through `RAPI_GL` and creates an SDL OpenGL ES context in `src/pc/gfx/gfx_sdl2.m`.

This is a good rasterization boundary. Add a `RAPI_METAL` implementation behind the same renderer API and leave `gfx_pc.c`'s game/display-list interpretation intact. Port the existing combiner semantics and sampler/depth/scissor behavior to Metal Shading Language and Metal pipeline state.

The current triangle API alone is **not enough for the later ray tracer**. `gfx_pc.c` has already transformed vertices through model-view-projection when it calls `draw_triangles`; the batch contains clip-space x/y/z/w, texture coordinates, fog values, and combiner inputs, but no local/world positions, mesh identity, or object transform. Add a backend-neutral scene-capture seam in `gfx_pc.c` before projection is applied. It should collect local-space geometry, stable mesh identity, per-frame instances/transforms, material and texture references, camera matrices, and lights into a frame scene description. Metal rasterization can initially ignore this scene description; the same representation can later feed either Metal acceleration structures or a compute BVH.

Prefer stable mesh/instance identity from geo-layout traversal where available, with a deterministic geometry hash as a fallback. This lets static geometry be cached, dynamic transforms be updated, and future ray tracing avoid rebuilding a flat list of projected triangles every frame.

## iOS application layer to preserve

- UIKit/Xcode app and landscape configuration already exist.
- `pc_main.m` attaches touchscreen callbacks to the game controller layer.
- `controller_sdl2.c` already uses SDL GameController for connected controllers; verify pairing and mapping on device.
- SDL2 owns the current audio and event loop. Its iOS audio path uses platform audio APIs; keep it initially and only replace it if device testing reveals a requirement.
- `cliopts.c` selects the app sandbox's `Documents` directory for save and resource paths.
- The current graphics host is SDL OpenGL ES, not MetalKit. The iOS host will need an `MTKView` or `CAMetalLayer` presentation surface, while retaining the touch overlay, game loop, and input routing.

## Milestones

1. **Baseline and build system:** keep the iOS fork's game sources; replace its hard-coded Xcode SDK path and brittle packaging step with reproducible iOS build settings. Keep ROM-derived assets local and out of source control.
2. **Metal rasterizer:** add the `RAPI_METAL` API selection, MetalKit-backed surface, command queue/render pass, MSL equivalents for the generated combiner shaders, texture upload/sampling, alpha/fog, depth, viewport/scissor, and frame pacing. Compare captured frames against the existing OpenGL ES output.
3. **Native app integration:** preserve the UIKit touch UI, SDL GameController input, sandbox save paths, audio path, and landscape orientation. Package an unsigned ARM64 IPA for sideloading; add signing only for a distribution route that needs it.
4. **Ray-tracing scene model:** introduce frame-scoped camera, mesh, instance, material, texture, and light records at the display-list decode boundary. Validate transforms, material IDs, and visibility on Linux with deterministic CPU-side capture/dump checks.
5. **Hybrid ray tracing:** query Metal ray-tracing capability at runtime. Use Metal acceleration structures and ray-intersection functions on hardware that provides acceleration. For A16, prototype a compute MSL BVH traversal at reduced resolution and measure it before committing to quality targets. Keep the rasterizer as the baseline and fallback.
6. **Effects in order:** add ray-traced shadows first, then selected reflections, then indirect light. Add temporal accumulation, motion rejection, and denoising only after per-frame output is correct.

## Build and validation limits

Linux can build the repository's host asset/tool utilities, validate portable scene-capture structures and algorithms, compare CPU-generated scene dumps, and compile non-Apple C/C++ code. It cannot compile Metal shaders, build the iOS application, sign an IPA, or validate GPU performance. Those gates need a macOS/Xcode build runner; final raster comparisons and A16 performance checks need a physical iPhone 14 Pro Max.

## RT64 legacy reference findings

The isolated MIT reference checkout and compatibility report are documented in [`RT64_COMPATIBILITY.md`](RT64_COMPATIBILITY.md). Carry its scene separation forward as reusable mesh geometry plus per-frame instances/materials/lights/camera, including previous transforms and motion flow for temporal validation. Its primary/direct/indirect/reflection/refraction passes and depth/normal history rejection are useful designs. Do not carry over DXR hit groups, D3D12 descriptors/barriers, or the NVIDIA acceleration-structure helper library. The A16 design remains a compute software BVH with cached per-mesh BLAS-like trees, a top-level instance hierarchy, explicit traversal limits, and performance measurements before effects beyond shadows.

The display-list capture now stores view and projection matrices once per frame, keeps local vertex attributes per triangle, and attempts to split object-to-world from model-view using `object * view = modelview` in the RSP row-vector convention. The split is marked valid only when a base-stack view matrix has been captured, the matrix is invertible, and the current stack path still inherits that view; a nested matrix load invalidates that claim. The root-level model-view load is currently treated as the view matrix by convention, so its meaning still needs confirmation from a real SM64 frame dump. Geometry IDs hash vertex position, normal, UV, and color contents; instance IDs hash that geometry key with the derived object transform, so unchanged static geometry/placement keys are repeatable across process launches. Animated geometry naturally receives a new geometry key. This removes source-address dependence but does not provide geo-layout node IDs, previous transforms, motion history, or light records. Keep the validity bit authoritative and do not feed invalid transforms to a BVH. Add a frame dump and confirm the view/object split on a captured game scene before treating this as a world-space ray-tracing input.

A host-only CPU BVH reference now accepts triangles already transformed into one world-space coordinate system. It validates flattened node construction, bounded traversal, and hit metadata, but it is not connected to captured triangles yet. Use it as the correctness oracle for the later Metal compute traversal after camera/object transforms are resolved; it does not provide ray-traced output in the game.

The pack audit is in [`RENDER96_TEXTURE_COMPATIBILITY.md`](RENDER96_TEXTURE_COMPATIBILITY.md). Keep original textures as the default and treat RENDER96 as a user-imported optional override only after runtime fallback, bounded residency, PNG-only initial support, sampler fidelity, and per-asset provenance are addressed. Do not enable eager `EXTERNAL_DATA` precaching for the pack on iOS.
