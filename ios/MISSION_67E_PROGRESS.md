# Mission 67E progress

Worktree `/home/larry/Projects/sm64ios-mission67e`, branch
`mission-67e-render-compatibility`, starting at
`5c50a9206f9061b85f3ac03defddc531a070cfb6`. This includes Mission 67B's
provider and the Mission 67D metadata audit. The exact 67D commits
`d3d0f20731d3544f036996cc7101435ab07ca5bc` and
`5c50a9206f9061b85f3ac03defddc531a070cfb6` are verified on GitHub.
No shared worktree or asset data is needed.

Implement WSC2 while continuing to read WSC1. WSC2 will store ordered
batches and bounded draw-state slots. The original authoring tool decodes
indexed inputs to RGBA PNGs before the existing Gfx upload path. Planned
validation covers raw upload bytes, alpha and depth state, scoped draw
state, package rollback and texture-name cleanup.

## Implementation checkpoint

The renderer changes are at
`7777e69bf61c2e6a9c80765a5e15bf1575bb6ee5`. The package ABI and
compatibility matrix are in `ios/MISSION_67E_RENDER_COMPATIBILITY.md`.
WSC2 writes and reads separate ordered opaque and translucent material
batches. The authoring tool generates RGBA PNGs for authored CI4 and CI8
textures. The interpreter uses validated draw-state slots for scroll
and tint, and restores the caller's state after each package pass.

Validation passed locally:

- `CC=clang sh tests/run_linux_checks.sh` including the constant-alpha
  regressions and the WSC1/WSC2 renderer replay.
- `ASAN_OPTIONS=detect_leaks=0 CC='clang -fsanitize=address,undefined -fno-sanitize-recover=undefined' python3 tests/test_world_scene_package.py`.
  Four tests passed. Leak detection was disabled.
- Shell syntax, Python bytecode, and `git diff --check` passed.

The branch was pushed and verified at the implementation SHA before
dispatching the existing asset-free
[Apple SDK validation run](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44383033844).
It builds validation binaries and generated MSL, with no full-game IPA.

## End of session

Run `44383033844` passed Linux and Apple jobs at the implementation SHA
above. The Apple job used Xcode 27.1 (`27A9275`) and iPhoneOS SDK 27.1 on
the standard `xcode-27` runner. It syntax-checked the feature-enabled
provider, Gfx interpreter and area hooks, linked arm64 device and simulator
Metal validation executables, and compiled 12 MSL variants into metallibs.
The downloaded validation artifact is in `build/mission67e/apple-validation`
and is excluded from Git. Linux and sanitizer logs are in `build/mission67e`.

Exact verified source SHA `7777e69bf61c2e6a9c80765a5e15bf1575bb6ee5`.
The follow-up commit records this checkpoint. Mission 67C can integrate
against the documented WSC2 interface on this branch. Next validation
requires a full authorized app build with authored fixtures and an iPhone
run to check blend order, depth occlusion, scrolling and resource residency.
The Kokiri setup and all real asset conversion and gameplay remain outside
this build. No IPA was created and no proprietary assets were uploaded.
