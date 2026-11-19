# Mission 72 — Fast3D Compatibility Report

## Result

**PARTIAL — Fast3D import compatibility expanded and host-validated; branch publication and Apple/device validation remain blocked.**

- Branch: `mission-72-fast3d-compat`
- Mission 72 starting baseline: `93a8f67be79dee48f2b67013c7fdc8bcf70853db`
- Previously committed local work: `2bb532c4cb4d9c6ff2dc17b8aaed51b506c5c90b`, `b9ee36f17757e68253fbbcdc35008566134b0fb4`
- Recovery implementation commit: `d4c4040d00692dd319e593e0236111c5d3955131`
- Documentation checkpoints: `546186d69ffcc8b13f1b35710d94afee834fde5b`, `39cd62f75d55dcda3d890b7186444d322d68ae7a`

## Implemented

- Static hashed/segmented affine model-view replay and position baking; supported 4/8/16-bit texture crops, including odd-nibble CI4 handling.
- Fog state tracing and WSC4 encoding for homogeneous per-material fog. Native scene loading accepts WSC4 while preserving WSC1/WSC2/WSC3 layouts. The renderer computes the source SM64 clip-depth fog factor and blends fog RGB in the scene-material Metal shader path.
- Static directional-plus-ambient `G_LIGHTING` conversion. The tracer resolves bounded `Light_t` records and per-vertex normals. When the combiner consumes shade RGB, the importer applies the source `calculate_normal_dir` and vertex-light equation, then bakes the result into the existing WSC3 vertex color. No new material format is needed for static lighting.
- Explicit rejection for incomplete/mixed fog state, missing or unsupported lighting state, positional lights, and unsupported texgen. No silent substitute colors are emitted.
- `ios/WSC4_FORMAT.md` specifies the versioned fog record and the compatibility boundaries.

## Validation

- **PASS:** `./tests/run_linux_checks.sh` completed with exit code 0, including the full Linux regression script and its host-side compilation/replay checks. Existing compiler warnings in `src/pc/platform.c` and `src/game/mario_actions_airborne.c` remain.
- **PASS:** Fast3D tile/TLUT and pass-state reference harnesses against pinned upstream checkouts, with ASan/UBSan. `ASAN_OPTIONS=detect_leaks=0` is required because LeakSanitizer is unavailable under this host's ptrace policy.
- **PASS:** `CC='clang -fsanitize=address,undefined -fno-omit-frame-pointer' ASAN_OPTIONS=detect_leaks=0 python3 tests/test_world_scene_package.py` — 5 package/runtime tests passed, including WSC4 fog loading, shader feature selection, fog-factor calculation, and teardown. Existing WSC1/WSC2/WSC3 coverage remains in the suite.
- **PASS:** Focused converter suites: `tests/test_o2r_draw_trace.py` (14 tests) and `tests/test_o2r_to_wsc3.py` (13 tests).
- **PASS:** `git diff --check`.
- **UNVERIFIED:** Apple/Xcode build and Metal compiler validation; `xcodebuild` is not installed.
- **UNVERIFIED:** Physical-iPhone rendering. Host MSL source generation/replay does not establish native Apple Metal execution or visual correctness.

## Real archive check

The locally supplied archive was read-only and remains outside Git. SHA-256:

`e3bf3ae3565768998026e60113ed3fc506fc5415e23299de41b9b42a5fbc3807`

A raw trace of `scenes/shared/spot04_scene/spot04_scene` found 3 rooms, 40 decoded display lists, 1,054 candidate draws, and 6 unresolved dynamic display-list calls. Without the required runtime/pass prelude, all 1,054 candidates are incomplete, including absent fog coefficients/color and incomplete render state. All records mark lighting enabled, but none consume `shade.rgb`; therefore static vertex lighting does not currently change this scene's traced output. This trace is not a valid Kokiri conversion and no scene was exported or called playable.

## Publication and build status

- **BLOCKED:** The shell cannot resolve `github.com`; the attempted Git push failed with `Could not resolve host: github.com`. No remote update is claimed.
- **BLOCKED:** No Apple validation workflow was run. The local checkout has no `xcodebuild`, and the established workflow does not provide a usable local substitute for Apple's SDK/Metal compiler.
- No IPA, private upload, or physical-device test was produced.

The initial local HEAD and full reachable Git history were preserved before implementation in `/tmp/sm64ios-mission72-b9ee36f.bundle` (verified with `git bundle verify`). The final local branch will be preserved separately at `/tmp/sm64ios-mission72-recovery-final.bundle`; the initial bundle remains untouched.

## Remaining compatibility limits

Dynamic runtime-generated display lists, runtime-varying lights, positional lighting, generated texture coordinates, several alpha/coverage/depth modes, scene actors, water boxes, camera scripts, and game events remain unsupported or explicitly rejected. Static matrix lowering assumes camera-independent model/world transforms. The local Kokiri trace requires accurate runtime pass initialization and resolution of six dynamic calls before a scene conversion can establish meaningful fog behavior.
