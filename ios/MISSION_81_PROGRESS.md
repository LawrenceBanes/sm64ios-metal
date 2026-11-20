# Mission 81 progress

## Current checkpoint

- Branch: `mission-81-shader64-rendergraph`
- Base: `ae34240a53ba77eefe28676e9eb6a7f741e24aaf` (`mission-79-shader64`)
- Code checkpoint: `d00525c2dfd8c418f78a1ee77c20e22ea1f76e16` (API 2 render graph)
- Current documentation checkpoint: `4442d743551c6f37169a04ddea01ff0a82121402`
- Status: first API 2 fullscreen DAG vertical slice committed, Apple-compiled, and packaged as a private unsigned device-test IPA; larger Iris-class stages remain incomplete.
- Physical-device output: not verified.

## Implemented in working tree

- Bounded API 2 graph validator and common per-pass Metal wrapper.
- iOS runtime pipeline preparation for each fullscreen pass, transactional graph activation, intermediate RGBA16Float targets, command-buffer completion lifetime, runtime target memory budget, and API 1 switching/Classic64 cleanup.
- iOS manifest handling for API 1 and API 2; API 2 startup restore reads original installed pass sources.
- Two-pass GraphDemo reference pack and Bliss camera-gridlines licensed adaptation pack.
- Apple Metal validation now compiles wrapped API 2 reference pass sources as well as the preserved API 1 packs.
- Added host checks for graph ordering, cycles, feedback, target limits, wrappers, pack import, and the API 2 examples.
- Architecture/resource contract and local Bliss archive compatibility map in `SHADER64_MISSION_81.md`.
- Fixed the GraphDemo `levels` range mismatch: the pack declares 2–16, while the runtime previously rejected/clamped values above 8. Float shader parameters now consistently allow a bounded ±16 across validation, persisted values, and Metal uniforms.
- Unified API 2 intermediate target sizing between host budget validation and Metal allocation using double-precision multiplication followed by floor-to-pixel sizing. A regression reproduces a 145-pixel dimension / `0x3ef3a431` scale rounding up to 69 under integer-to-float multiplication, where the encoded float's exact product is below 69 and must allocate 68.
- Production ZIP-importer coverage now packages and imports every checked-in reference pack (Classic64, Paper64, Lumen64, GraphDemo, BlissGridlines), verifies extracted source payloads, validates API signatures/graph structure, and checks declared float parameter bounds.

## Tests so far

- `python3 ios/test_shader64_pack.py`: passed after correcting the oversized-target fixture to exceed 32 MiB at 2048x1080. Includes ZIP smoke tests under ASan/UBSan.
- `python3 ios/test_shader64_pack.py`: passed, 14 tests, including the float rounding boundary, the full reference-pack production-importer matrix, and ZIP smoke tests under ASan/UBSan.
- `tests/run_linux_checks.sh`: passed (complete suite, exit 0), including API 2 source wrapper and archive tests. Existing platform/action compiler warnings and fixture-invalid-scene diagnostics remain in suite output.
- `git diff --check`: passed at the previous checkpoint; rerun after remaining edits.
- Apple validation workflow `44787616391` passed both Linux and Xcode 27 iOS device/simulator compile/link jobs, including UIKit integration and wrapped API 2 GraphDemo/Bliss Metal sources. No GPU or iPhone runtime was executed.
- Full IPA workflow `44787935139` passed Linux checks, Xcode 27 Metal validation, full iPhoneOS arm64 app build, IPA validation, and private release upload. Source SHA: `4442d743551c6f37169a04ddea01ff0a82121402`.
- Private draft release: https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/untagged-17f166d22fdc8261aac8
- IPA asset: `sm64ios-connected-world-arm64-unsigned.ipa`, 9,385,655 bytes, SHA-256 `e6c6731ae20708f24423fe8babf360acbefd07dc5ab3ee71f7e9959adf017e25`. Download: https://github.com/LawrenceBanes/sm64ios-private-builds/releases/download/untagged-17f166d22fdc8261aac8/sm64ios-connected-world-arm64-unsigned.ipa
- Verified embedded app identity: bundle ID `com.larry.shitboxdev`, display name `SHITBOX LABZ INC.`, version `0.69.420` (build `1`). The private-build repository is confirmed private.
- Apple compilation and packaging are verified by CI; Metal GPU execution and physical iPhone output are not verified.

## Remaining work / next

1. Install the private IPA on a test iPhone, prepare each reference pack directory as a ZIP, and import it through the in-game Graphics → Shader Pack → Import ZIP Pack flow; activate GraphDemo or BlissGridlines and record visual/runtime diagnostics.
2. Extend resource/node contracts only after providing corresponding engine data. Bliss temporal, depth, G-buffer, geometry, shadow, compute, texture and Minecraft-uniform requirements remain unsupported.

Mission 81 is **PARTIAL**. API 1 preservation and host validation are necessary but do not establish GPU output, iPhone behavior, or Iris/OptiFine compatibility.
