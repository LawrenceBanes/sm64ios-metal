# Mission 81 progress

## Current checkpoint

- Branch: `mission-81-shader64-rendergraph`
- Base: `ae34240a53ba77eefe28676e9eb6a7f741e24aaf` (`mission-79-shader64`)
- Status: API 2 fullscreen DAG implementation and Bliss workload mapping in progress; not yet committed or Apple-compiled.
- Physical-device output: not verified.

## Implemented in working tree

- Bounded API 2 graph validator and common per-pass Metal wrapper.
- iOS runtime pipeline preparation for each fullscreen pass, transactional graph activation, intermediate RGBA16Float targets, command-buffer completion lifetime, runtime target memory budget, and API 1 switching/Classic64 cleanup.
- iOS manifest handling for API 1 and API 2; API 2 startup restore reads original installed pass sources.
- Two-pass GraphDemo reference pack and Bliss camera-gridlines licensed adaptation pack.
- Apple Metal validation now compiles wrapped API 2 reference pass sources as well as the preserved API 1 packs.
- Added host checks for graph ordering, cycles, feedback, target limits, wrappers, pack import, and the API 2 examples.
- Architecture/resource contract and local Bliss archive compatibility map in `SHADER64_MISSION_81.md`.

## Tests so far

- `python3 ios/test_shader64_pack.py`: passed after correcting the oversized-target fixture to exceed 32 MiB at 2048x1080. Includes ZIP smoke tests under ASan/UBSan.
- `tests/run_linux_checks.sh`: passed (complete suite, exit 0), including API 2 source wrapper and archive tests. Existing platform/action compiler warnings and fixture-invalid-scene diagnostics remain in suite output.
- `git diff --check`: passed at the previous checkpoint; rerun after remaining edits.
- Apple CI not yet run for this branch.
- No Metal SDK on this host assumed; Apple compilation is not claimed.

## Remaining work / next

1. Review and fix Objective-C/UIKit/Metal compile issues; run local syntax checks where possible.
2. Add graph source/manifest/validation regressions, run the full Linux suite and sanitizers.
3. Run Xcode 27 validation and iPhoneOS arm64 compile CI; diagnose failures.
4. Confirm runtime multipass pixel change on an actual device (not available in host test).
5. Extend resource/node contracts only after providing corresponding engine data. Bliss temporal, depth, G-buffer, geometry, shadow, compute, texture and Minecraft-uniform requirements remain unsupported.

Mission 81 is **PARTIAL**. API 1 preservation and host validation are necessary but do not establish GPU output, iPhone behavior, or Iris/OptiFine compatibility.
