# SHADER64 Progress

## Checkpoint

- Branch: `mission-79-shader64`
- Base checkpoint: `24b757afec596d883ad5aee09719f75d8eba15ff`
- Current work: runtime postprocess shader pack vertical slice
- Physical Apple device output: not tested

## Implemented in this increment

- Versioned API 1 manifest validation and bounded ZIP installation into app Documents storage through the iOS Files picker.
- ZIP policy: max 8 MiB compressed/expanded total, 64 central-directory records, 10 recognized files, 64 KiB manifest, 256 KiB per Metal source; stored/deflate only; validates CRC, rejects ZIP64, encryption, traversal, symlink, duplicate, unknown-file, and malformed paths. Only root `manifest.json` and direct `shaders/*.metal` files are extracted.
- Runtime Metal source compilation with `newLibraryWithSource:options:error:`, pipeline creation, final-composite activation, parameter bytes, persisted pack selection, and Classic64 restore on missing/failed startup loads.
- Hot switching to Classic64 keeps the prior material rasterizer and draws the native HUD after scene composition. Failed compile/pipeline creation leaves the currently active shader unchanged.
- Shader manager UI supports Files import, installed pack selection, parameter entry bounded by manifest metadata, and removal. Runtime compilation is not JIT and does not require resigning the application.
- Paper64 and Lumen64 reference packs demonstrate image-neighbor cel outlines/quantization and an exposure/saturation composite. Classic64 is the built-in original renderer.
- Added public pack/API documentation and Apple validation compilation of complete runtime-wrapped sample source.

## Tests

- `python3 ios/test_shader64_pack.py`: ZIP, manifest-source signature, wrapper generation, cleanup, malformed data, and ASan/UBSan smoke cases passed.
- `tests/check_metal_project_wiring.sh`: passed.
- `CC=clang tests/run_metal_shader_codegen_test.sh`: passed.
- `tests/run_linux_checks.sh`: passed on 2026-11-20. Existing unrelated platform/action warnings were printed; no suite failure.
- `git diff --check`: passed.
- `xcrun` is unavailable in this local environment. Apple Metal compilation and iPhoneOS arm64 compilation remain pending GitHub Actions.

## Explicit current API 1 limitations

- Only the final postprocess stage is replaceable. It gets the composited scene color texture, normalized UV, sampler, and four floats.
- No custom world/character material shaders, geometry attributes, camera/depth/normal resources, shadow pass, textures, animation, previous-frame input, pass graph/dependencies, device capability declarations, dependency resolution, or metallib-only packs yet.
- While a custom shader is selected, it replaces the built-in final composite. Built-in AO, shadow receiver, fog, bloom, tone mapping, FXAA, sky, and light-shaft final composition are bypassed; SM64 materials, rasterization, and HUD remain native.
- Metal source is untrusted and not sandboxed from GPU execution. Input size and extraction are bounded, but GPU hangs/expensive shaders cannot be guaranteed safe by source validation.
- No actual device, visual, thermal, or performance evidence yet. Apple validation compiles sample wrapper MSL but cannot prove image output.

## Next

Run GitHub Apple Metal validation on this branch; repair actual compiler findings. Then run the iPhoneOS compile-only/full app build if the validation succeeds. Do not claim device pixels until the IPA is installed and inspected on hardware.
