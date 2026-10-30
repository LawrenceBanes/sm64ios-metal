# Native Metal implementation progress

## Current state

- Selected the direct `sm64ex-ios` base and retained the existing `GfxRenderingAPI`, Fast3D interpreter, UIKit touch overlay, SDL2 controller/audio paths, and save path.
- Added `gfx_metal_shader.c`, a portable shader-ID-to-MSL generator that derives input and texture features through the existing combiner feature decoder. It generates color/alpha combiners, texture reads, texture-edge discard, fog, and optional three-point filtering. Added Linux checks for untextured/textured, alpha, fog, texture-edge, filtering, and buffer-capacity cases.
- Added an initial `RAPI_METAL` implementation using MetalKit/MTKView, dynamic MSL compilation, pipeline state, texture upload, per-tile samplers, blending, depth, viewport, scissor, and manual command-buffer presentation. The SDL/UIKit window integration now has a non-OpenGL mode for iOS Metal.
- Updated iOS build scripts to resolve SDK/compiler paths with `xcrun`, select `RENDER_API=METAL` for iOS, and retain GL for the separate tvOS target. Added Metal source and MetalKit references to the Xcode project.
- Added an opt-in scene capture buffer before projection output. It records local vertex positions, normals, UVs, shade colors, model-view/projection matrices, combiner and texture IDs, plus stable-in-process source keys for geometry and matrix paths. It is disabled by default and drops excess triangles at a fixed bound.
- Audited the isolated MIT RT64 legacy checkout. Its reusable conceptual model and ray-pass findings are in [`RT64_COMPATIBILITY.md`](RT64_COMPATIBILITY.md); no RT64 source was copied.

## Validation

- `tests/run_metal_shader_codegen_test.sh`: passing on Linux (portable C code only; this does not compile MSL or the Objective-C Metal backend).
- `tests/run_scene_capture_test.sh`: passing on Linux (disabled/enabled storage, copied vertex/matrix values, and stable-key checks).
- `tests/run_linux_checks.sh`: passing; runs both portable test programs, Clang syntax checks for `gfx_pc.c`, scene capture, and MSL code generation under both `RAPI_GL` and `RAPI_METAL`, then builds host tools.
- `bash -n build.sh build_ios.sh tests/run_linux_checks.sh tests/run_scene_capture_test.sh` and `git diff --check`: passing.
- `make -C tools -j2`: passed earlier in this checkout; only existing toolchain deprecation/format warnings were emitted.
- Full SM64 build remains blocked on the absent `baserom.us.z64` and ROM-derived assets. `xcodebuild` and Apple SDKs are not installed in this Linux environment.

## Outstanding work

- Review Metal API semantics on Apple SDKs: compile generated MSL, validate sampler wrap encoding, viewport/scissor orientation, depth/decal state, and pipeline/resource lifetime.
- Finish ARM64 build dependencies; the existing SDL2 headers/static library and `ioslib` paths are missing from this Linux checkout.
- Refine scene identity and separate the camera view from object transforms. Current keys hash source addresses and matrix-source paths, and the capture records model-view plus projection; this is not yet a world-space scene or previous-frame motion capture.
- Add capture of relevant light state and a deterministic frame dump. Add previous-transform history keyed by a validated stable instance ID.
- Add host-side BVH structures/build/traversal tests before writing Metal compute traversal. No ray tracing is implemented or validated yet.
- Compare Metal raster frames with the retained OpenGL ES backend and validate lifecycle, audio, touch, controllers, saves, scaling, and landscape orientation on a physical iPhone.
- Build, sign as appropriate, and install an IPA via SideStore after Xcode/device validation.

## Next steps

1. Continue reviewing Metal integration and preserve the selectable OpenGL ES fallback.
2. Refine and test world-space scene capture, stable IDs, and previous transforms without changing normal raster submission.
3. Add host-side BVH build/traversal tests before implementing Metal compute traversal.
4. Set up a reproducible macOS/Xcode build lane once a runner is available; do not treat Linux checks as an iOS build result.
