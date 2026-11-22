# Mission 82 checkpoint — 2026-11-22

## Where to resume

- Worktree: `/home/larry/Projects/sm64ios-mission82`
- Branch: `mission-82-coopdx-native`
- Checkpoint commit: the handoff commit on this branch; do not merge this
  branch into `main`.
- Mission 81 remains in its separate worktree/branch and was not modified.
- The original Character Select ZIP is local only at
  `/tmp/mission82-character-select-v1.16.3.zip`; SHA-256:
  `3cef0c66a1e35f6957ff7f3e1b476f7d48ad2713c80049cf982338bba1fdd1a9`.
  It was downloaded from the official mod page's v1.16.3 author release. The
  ZIP and extracted payload are not committed.

## Verified at this checkpoint

- `tests/mods/run_host_tests.sh` passes on the current source. It exercises the
  production ZIP importer, Lua bridge, audio sample cleanup, HUD texture draw
  submission, and synthetic DynOS integration.
- `tests/run_linux_checks.sh` completed successfully. Existing warnings in
  `mario_actions_airborne.c` and diagnostic output from malformed-scene tests
  remain unchanged.
- `git diff --check` passed before the handoff commit.
- The official Character Select ZIP imports through the production importer.
  Its Lua startup has progressed through the original package's texture
  discovery/decoding, custom animation registration, bounded menu-theme and
  sound loading, and the registered `HOOK_ON_GEO_PROCESS` event. It is **not
  initialized successfully** and is not a working selectable-character
  feature.

## Preserved implementation, with incomplete parts clearly gated

The checkpoint includes prior uncommitted compatibility work plus these latest
changes:

- Bounded menu audio now permits up to 120 seconds per stream and an 8 MiB
  per-mod PCM budget (16 MiB global); short samples remain limited to 15 seconds.
  `audio_stream_set_loop_points` validates source-frame positions and tolerates
  only up to 50 ms of duration-rounding overrun, clamping that to the decoded
  endpoint. The current implementation still decodes each stream fully to PCM;
  it is not a streaming decoder.
- Exposed `CHAR_ANIM_MAX` and player palette constants. The single-player
  palette getter returns upstream default Mario palette colors; actual palette
  overrides are not integrated into native material rendering.
- Added `gTextures` with known stock HUD asset references where present. The
  alternate character head textures are absent. Host tests do not validate the
  iOS `EXTERNAL_DATA` asset resolution or GPU texture output for these mappings.
- Added an ABI-preserving graph hook flag, GeoLayout hook dispatch, and
  `geo_get_current_root` viewport access to begin supporting the real
  Character Select GeoLayout callback. This iOS rendering call path has **not**
  been built by the host-only test command or exercised in-game; treat it as
  work in progress.
- Earlier changes in the same working set include Date/console/file API
  compatibility, zero-based `gActiveMods`, dialog hooks, HUD flag mapping, and
  other startup APIs reached by Character Select. See commit diff and source
  history for exact implementations.

## Current concrete blocker

Running
`MOD_EXTERNAL_ZIP=/tmp/mission82-character-select-v1.16.3.zip tests/mods/run_host_tests.sh`
imports the unmodified ZIP but startup stops at the first unresolved symbol:

`hook_event(HOOK_ON_SYNC_VALID, set_all_visuals)` — `HOOK_ON_SYNC_VALID` is not
yet exposed or dispatched. Implement its single-player meaning carefully (the
local player becomes valid after mods start), add callback tests, and rerun the
same original ZIP. Expect further missing APIs after this blocker; do not treat
the ZIP import or partial startup as mod compatibility.

## Next milestone

Continue the compatibility loop on Character Select v1.16.3: implement and test
`HOOK_ON_SYNC_VALID`, run the original package to its next failure, and proceed
through startup and actual Character Select menu callbacks. Then obtain the
selected original Extra Characters PLUS package and validate its actor/model
path. Character Select itself contains no character actor `.bin`, `.col`, or
`.lvl` bundle. Do not claim the first-milestone selectable character until an
original compatible character pack registers and renders through Metal.

## Validation not done for this checkpoint

- No Apple/Xcode compilation was run against these latest changes. The last
  known workflow, run `44992333348`, failed on its prior tested commit
  `de0aea355908a3300b058e71e812a96f532e0930`; inspect its logs before relying on
  it.
- No iPhone runtime, actual Character Select menu, palette rendering, audio
  output, Metal pixels, or physical-device behavior was verified.
- The original Character Select production-import startup test is currently
  expected to fail at `HOOK_ON_SYNC_VALID`.

