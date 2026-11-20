# SHADER64 Progress

## Checkpoint

- Branch: `mission-79-shader64`
- Base checkpoint: `24b757afec596d883ad5aee09719f75d8eba15ff`
- Runtime postprocess shader pack vertical slice: `9f47ddf3aefb8ab53542c067d868a3ee4af698f8`
- Current work: API v1 postprocess-only pack support; broader render stages remain unfinished
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
- Apple Metal/Xcode validation: [workflow 44772126993](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44772126993) passed Linux checks and iOS device/simulator Metal backend compile/link, including wrapped reference pack MSL.
- Full IPA workflow: [run 44772390342](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44772390342) passed Linux checks, Xcode 27 Metal/MSL validation, full iPhoneOS arm64 app build, packaging and private upload. The IPA was built from commit `9f47ddf3aefb8ab53542c067d868a3ee4af698f8`.
- Private draft release: [unsigned build 44772390342.1](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-9f1ed7aa4c78936135a2). Asset `sm64ios-connected-world-arm64-unsigned.ipa`, 9,375,182 bytes, SHA-256 `c1074ab3e43242e209515c89f24e430e73a1e92c7955ff9167e467802d96d9ef`.
- `xcrun` is unavailable locally; Apple build evidence comes from the GitHub-hosted Xcode 27 workflows. No physical iPhone rendering, visual parity, performance, thermal, or GPU-workload safety has been verified.

## Explicit current API 1 limitations

- Only the final postprocess stage is replaceable. It gets the composited scene color texture, normalized UV, sampler, and four floats.
- No custom world/character material shaders, geometry attributes, camera/depth/normal resources, shadow pass, textures, animation, previous-frame input, pass graph/dependencies, device capability declarations, dependency resolution, or metallib-only packs yet.
- While a custom shader is selected, it replaces the built-in final composite. Built-in AO, shadow receiver, fog, bloom, tone mapping, FXAA, sky, and light-shaft final composition are bypassed; SM64 materials, rasterization, and HUD remain native.
- Metal source is untrusted and not sandboxed from GPU execution. Input size and extraction are bounded, but GPU hangs/expensive shaders cannot be guaranteed safe by source validation.
- No actual device, visual, thermal, or performance evidence yet. Apple validation compiles sample wrapper MSL but cannot prove image output.

## Next

Next: extend the versioned contract beyond the single postprocess replacement only where real renderer resources and semantics can be supplied; keep unsupported stages unavailable. Device installation and pixel-level output remain an iPhone test task. Do not claim device pixels until the IPA is installed and inspected on hardware.
