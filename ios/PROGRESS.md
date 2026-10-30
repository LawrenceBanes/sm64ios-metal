# Native Metal implementation progress

## Current state

- Selected the direct `sm64ex-ios` base and retained the existing `GfxRenderingAPI`, Fast3D interpreter, UIKit touch overlay, SDL2 controller/audio paths, and save path.
- Added `gfx_metal_shader.c`, a portable shader-ID-to-MSL generator that derives input and texture features through the existing combiner feature decoder. It generates color/alpha combiners, texture reads, texture-edge discard, fog, optional three-point filtering, and the GL-compatible frame-varying alpha-noise dither. Added Linux checks for untextured/textured, alpha, fog, edge discard, filtering, noise, and buffer-capacity cases.
- Added an initial `RAPI_METAL` implementation using MetalKit/MTKView, dynamic MSL compilation, pipeline state, texture upload, per-tile samplers, blending, depth, viewport, scissor, and manual command-buffer presentation. The SDL/UIKit window integration now has a non-OpenGL mode for iOS Metal.
- Extracted sampler wrap and pipeline-key translation into portable C helpers and added Linux tests for repeat/mirror/clamp and all four pipeline-state bits. The Objective-C backend now uses those helpers; Apple SDK compile/link has passed, while runtime rendering semantics remain unverified.
- Added a host-only CPU scene BVH reference for caller-provided world-space triangles. It builds a flat binary hierarchy with bounded-depth median splits and performs bounded nearest-hit traversal with material/instance metadata. This is not connected to capture or rendering, and is not a Metal ray-tracing implementation.
- Updated iOS build scripts to resolve SDK/compiler paths with `xcrun`, select `RENDER_API=METAL` for iOS, and retain GL for the separate tvOS target. Added Metal source and MetalKit references to the Xcode project.
- Added an opt-in scene capture buffer before projection output. It records local vertex positions, normals, UVs, shade colors, model-view/projection matrices, combiner and texture IDs, plus stable-in-process source keys for geometry and matrix paths. It is disabled by default and drops excess triangles at a fixed bound.
- Audited the isolated MIT RT64 legacy checkout. Its reusable conceptual model and ray-pass findings are in [`RT64_COMPATIBILITY.md`](RT64_COMPATIBILITY.md); no RT64 source was copied.
- Audited RENDER96's `gfx/actors`, `gfx/levels`, and `gfx/textures` layout against the existing `EXTERNAL_DATA` path. Findings and the optional override design are in [`RENDER96_TEXTURE_COMPATIBILITY.md`](RENDER96_TEXTURE_COMPATIBILITY.md); no pack assets were cloned or added.
- Prepared a ROM-independent Apple validation harness and GitHub Actions workflow. The harness compiles and links the Metal backend for device and simulator SDKs, emits representative MSL and compiles iOS AIR/metallib outputs, and checks the actual source/framework wiring. The workflow also runs the Linux suite and uploads logs/artifacts.
- Fixed the first CI run's Linux `tabledesign` linker failure by making the bundled audiofile archive an explicit prerequisite and linking that exact archive path. The macOS Apple SDK job now runs independently of Linux so host tool regressions do not suppress Apple compiler diagnostics.
- Corrected validation-script error propagation and host macOS SDK selection after run `41108917814` showed successful device/simulator backend links but exposed that `tee` masked a later shader-generator failure. Run `41109271236` then passed the complete check on Xcode 16.4; run `41109502266` passed again on Xcode 27.1 / iOS SDK 27.1.

## Validation

- `tests/run_metal_shader_codegen_test.sh`: passing on Linux (portable C code only; this does not compile MSL or the Objective-C Metal backend).
- `tests/run_scene_capture_test.sh`: passing on Linux (disabled/enabled storage, copied vertex/matrix values, and stable-key checks).
- `tests/run_linux_checks.sh`: passing; runs shader generation, sampler/pipeline state translation, and scene-capture tests; Clang syntax-checks `gfx_pc.c`, scene capture, and shader generation under both `RAPI_GL` and `RAPI_METAL`; checks that the Metal Objective-C file excludes cleanly on non-Apple hosts; and builds host tools.
- `tests/check_metal_project_wiring.sh`: passing. The GitHub Actions workflow YAML parses, and `bash -n` passes for the Apple validation and wiring scripts.
- `tests/run_scene_bvh_test.sh`: passing for hierarchy construction, nearest hit, misses, ray bounds, rebuild/destroy, and invalid-depth handling. The same test passed AddressSanitizer and UBSan with `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer is unavailable under this container's ptrace restrictions.
- `make -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL` confirms the Makefile selects `sm64ios`, defines `RAPI_METAL`, and includes the new Metal sources. A full target dry run cannot reach compilation because the ROM image required for asset extraction is absent.
- `bash -n build.sh build_ios.sh tests/run_linux_checks.sh tests/run_scene_capture_test.sh tests/run_metal_state_test.sh tests/run_scene_bvh_test.sh` and `git diff --check`: passing.
- `make -C tools -j2`: passed earlier in this checkout; only existing toolchain deprecation/format warnings were emitted.
- GitHub Actions run `41108219376` passed shader generation, state translation, scene capture, and CPU BVH checks, then failed linking `tools/tabledesign` against audiofile symbols. The fix is covered by clean parallel host-tool builds; macOS was skipped on that run but has since run independently and passed.
- GitHub Actions run `41109502266` passed both jobs. Linux passed shader generation, state mapping, scene capture, CPU BVH and host-tool builds, including `tabledesign` linked to the bundled audiofile archive. Xcode 27.1 (build `27A9275`, iOS SDK 27.1) compiled and linked the Objective-C Metal backend for arm64 iOS device and simulator SDKs without signing; it generated five representative shader variants and compiled each with Apple's Metal compiler into AIR and metallib outputs. The project source/framework wiring check passed. The run uploaded the compiler log, device/simulator validation executables, emitted MSL, AIR, and metallibs. This verifies compilation only; it did not launch the executables, run the iOS app, or validate iPhone rendering.
- Full SM64 build remains blocked on the absent `baserom.us.z64` and ROM-derived assets. `xcodebuild` and Apple SDKs are not installed in this Linux environment.
- Device/simulator Objective-C backend compilation/linking and MSL compilation/linking passed against the requested iOS 27.1 SDK in run `41109502266`. No iPhone rendering or IPA installation is yet validated.

## Outstanding work

- Review Metal API runtime semantics: validate sampler wrap encoding, viewport/scissor orientation, depth/decal state, and pipeline/resource lifetime on simulator/device.
- Continue reading GitHub Actions logs and artifacts for regressions; the ROM-independent compile lane now passes, while full game/app compilation and runtime checks remain pending.
- Finish ARM64 build dependencies; the existing SDL2 headers/static library and `ioslib` paths are missing from this Linux checkout.
- Refine scene identity and separate the camera view from object transforms. Current keys hash source addresses and matrix-source paths, and the capture records model-view plus projection; this is not yet a world-space scene or previous-frame motion capture.
- Add capture of relevant light state and a deterministic frame dump. Add previous-transform history keyed by a validated stable instance ID.
- Resolve camera/object transforms, then feed world-space capture into the CPU BVH and compare Metal compute traversal against this reference. No ray tracing is implemented in the game or validated on GPU.
- Compare Metal raster frames with the retained OpenGL ES backend and validate lifecycle, audio, touch, controllers, saves, scaling, and landscape orientation on a physical iPhone.
- Build, sign as appropriate, and install an IPA via SideStore after Xcode/device validation.

## Next steps

1. Continue reviewing Metal integration and preserve the selectable OpenGL ES fallback.
2. Refine and test world-space scene capture, stable IDs, and previous transforms without changing normal raster submission.
3. Resolve camera/object transforms and connect world-space capture to the CPU reference BVH before implementing Metal compute traversal.
4. Set up a reproducible macOS/Xcode build lane once a runner is available; do not treat Linux checks as an iOS build result.
