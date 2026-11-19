# Mission 69 — CoopDX Compatibility Audit (M1 checkpoint)

**Branch:** `mission-69`, based on Mission 68 commit `cff7144687716ea18ae964c3fce45325bc3c3122`  
**Audit status:** partial; exact Possess Anything package source remains unavailable in this workspace  
**Runtime target:** single-player iOS Metal, with host-side execution evidence kept separate

## Target identification

I could not find a CoopDX release titled **Crappy**. Two real releases match the possession/moveset description and are audited separately:

| Candidate | Upstream evidence | Dependency/package notes |
| --- | --- | --- |
| **Possess anything 1.1**, by kikwee | Its official mod page lists two downloadable Lua files, `entity-hide-and-seek.lua` (8.5 KB) and `entity-possession-mod.lua` (6.1 KB), and describes possession of entities such as enemies, coins, trees, signs, and stars. The author says the feature began while making a Koopa possession mod. | The author later describes it as an add-on to the Odyssey mod to make Cappy work. No license was displayed on the mod page. The download endpoint returned a cache miss here, and the browser host was unavailable, so I have not inspected or executed its Lua source. I will not describe unobserved API calls as requirements. |
| **Odyssey Mario's Moveset: Rebirth 1.4 (OMM Rebirth)**, by PeachyPeach | The official page lists Odyssey moves including Cappy, custom animations, an Odyssey HUD, custom camera controls, and settings opened with `/omm`; `/stars` opens a star interface. | The page does not list a separate required mod. The release metadata in its public source snapshot declares `incompatible: moveset`, which is a conflict declaration, not a dependency. The linked GitHub repository has no declared license (`license: null` and no root `LICENSE` entry), so no source or assets were added to this branch. |

Sources: [Possess anything 1.1](https://mods.sm64coopdx.com/mods/possess-anything.802/), [its download listing](https://mods.sm64coopdx.com/mods/possess-anything.802/download), [OMM Rebirth 1.4](https://mods.sm64coopdx.com/mods/odyssey-marios-moveset-rebirth.125/), [OMM public source repository](https://github.com/PeachyPeachSM64/coopdx-mods-omm-rebirth/tree/536b5ed0bc88fd777250d3701cb876029582a2a1).

## OMM source/package shape

The public OMM source snapshot at commit `536b5ed0bc88fd777250d3701cb876029582a2a1` contains 193 files: 89 `.lua`, 13 `.bin`, one `.bhv`, 80 `.tex`, and 10 `.ogg`. It includes actors, data, sound, and textures directories. The Lua set contains paired `-32.lua` and `-64.lua` chunks. The chunks are PUC Lua 5.3; header byte 13 stores `sizeof(size_t)` and is `4` in `cappy-events-32.lua` and `8` in `cappy-events-64.lua`. The corresponding Mario hook files have the same split.

Upstream sorts mod files by relative path, runs root Lua files in that order, and checks each bytecode header against the current Lua ABI. A chunk with a different `size_t` width fails that script load, while the loop continues to subsequent scripts. Mission 68's loader instead treated every root script failure as a fatal mod startup failure. That made a valid pair of 32-bit and 64-bit files incompatible on 64-bit iOS even if the native-width script was present. The new loader preflight skips only exact Lua 5.3 chunks whose `size_t` width is 4 or 8 and differs from the current process, then emits a diagnostic and continues. Other malformed chunks still fail normally.

References: upstream [`mod.c`](https://github.com/coop-deluxe/sm64coopdx/blob/8cd6e5977d9f920d51ca71f2c61801d019ed79c6/src/pc/mods/mod.c), [`smlua.c`](https://github.com/coop-deluxe/sm64coopdx/blob/8cd6e5977d9f920d51ca71f2c61801d019ed79c6/src/pc/lua/smlua.c), and the [Lua hook reference](https://github.com/coop-deluxe/sm64coopdx/blob/main/docs/lua/guides/hooks.md).

## Mission 68 capability comparison

| OMM subsystem | Existing Mission 68 support | Audit result against OMM |
| --- | --- | --- |
| Lua 5.3 source/bytecode | PUC Lua 5.3.5 interpreter; restricted standard libraries; source and bytecode load through the same runtime | Native-width OMM Lua 5.3 chunks are format-compatible. Wrong-width twins previously killed startup; M69 now skips those chunks. This is host-tested, not iPhone-tested. |
| `hook_event`, `hook_mario_action` | A small event subset and custom `every_frame`/`gravity` actions are connected to Mario update/action paths. `HOOK_BEFORE_PHYS_STEP` now uses the upstream integer-output behavior. | Partial. The step hook runs before ground, air, water, and hanging physics; it receives Mario state, step type, and step argument, and a returned integer cancels the native step and supplies the result. Most CoopDX event IDs remain unsupported. OMM's action constants and action-hook code have not been exercised through a complete gameplay run. |
| Behavior hooks and objects | New local behaviors, a limited object-field allowlist, and native-model object spawning | Partial. OMM's compiled scripts reference `spawn_non_sync_object`, `spawn_sync_object`, extended model lookup, object iteration and destruction, and broader object helpers. These are not registered in M68. `hook_behavior` also does not implement upstream's complete six-argument signature or native behavior replacement. |
| Mario physics and interactions | Bounded Mario-state fields, `set_mario_action`, `mario_set_forward_vel`, `set_mario_animation`, action hooks, the four-path pre-physics override hook, direct ground/air/water step calls, and one post-interaction event | Partial. Lua can now invoke those three native physics steps and receive their actual result, but floor/water queries, additional Mario-state transitions, and object interaction helpers remain incomplete. |
| DynOS models, textures, animations, collision | ZIP extraction and asset inventory only; assets remain inactive | Unsupported. The OMM source tree includes `.bin`, `.bhv`, and `.tex` resources and references `smlua_model_util_get_id`, `smlua_collision_util_get`, and `smlua_anim_util_register_animation`. No dynamic actor decoder, registry, collision loader, or Metal activation path exists. This blocks authentic OMM visuals and many moves. |
| HUD, menus, camera | HUD event hook and basic native mod-menu controls | Unsupported/partial. OMM's HUD code references `djui_hud_*` and `get_texture_info`; its command-driven `/omm` and `/stars` interfaces need usable chat-command UI and menu/render APIs. Camera mode and camera update APIs are missing. |
| Audio | Existing native SM64 sound engine only | Unsupported for OMM samples. The first OMM startup chunk calls `audio_sample_load`; M68 has no sample loader/player or `.ogg` decoder/registration. The OMM source contains `.ogg` files and references `audio_sample_play`. |
| Storage/save data | Bounded per-mod `mod_storage_*` plus selected save-file getters | Partial. Selected save functions are bound; the full save/star counter and custom resource semantics are not. |
| Multiplayer | Local player and local table adapters only | Unsupported for synchronization. OMM symbols include `network_*`, `gGlobalSyncTable`, and sync-table hooks. Local single-player execution must not pretend to replicate those values. |

The symbol scan of the 39 64-bit OMM Lua bytecode files found examples spanning HUD/audio, object and behavior management, physics, animation, camera, model/collision registration, and networking. Static bytecode strings are an inventory aid, not proof that every string is an executed global call. The host execution below confirms the first blocking call directly.

## Execution evidence

### Original community ZIP already present in Mission 68

Mission 68's unmodified MIT-licensed **SM64 Coop Achievements 2.1.0** ZIP remains the only full original ZIP tested in this runtime. It imports, starts, registers its menu, executes a Mario update callback, and persists its first-star achievement in the host harness. Calls to `play_sound(soundBits, gGlobalSoundSource)` now reach the native sound engine; the remaining `djui_hud_set_resolution` call is diagnosed and isolated. This is host evidence, not iPhone gameplay evidence.

### OMM source probe (not a ZIP import)

I used the unmodified `main.lua` metadata and four pairs of root bytecode scripts from the public OMM source snapshot. Those nine files were placed only in `/tmp` and are not committed. The host runtime:

1. Discovered the mod folder.
2. Skipped `a-bunch-of-constants-32.lua` on the 64-bit host with a `size_t`-width diagnostic.
3. Started the native-width `a-bunch-of-constants-64.lua` and failed on `unsupported CoopDX API 'audio_sample_load'`.
4. Disabled the mod under the current startup-failure policy.

This proves the corrected architecture handling reaches an authentic OMM root script and identifies its first runtime blocker. It does **not** import the original ZIP, execute the full mod, load its assets, or validate gameplay. The public source repository is not a licensed fixture; its files are not checked in.

The possession archive has not been tested: the download listing is visible, but the downloadable script source could not be fetched in this environment. Exact API/dependency conclusions for that archive remain open.

## M69 changes in this checkpoint

- Skip only PUC Lua 5.3 bytecode with an opposite 4-byte/8-byte `size_t` width during root-script startup; report the skipped file and widths.
- Add a regression that generates matching and opposite-width bytecode and verifies only the native-width script changes Mario state while the mod remains enabled.
- Add optional `MOD_EXTERNAL_ROOT` support to `tests/mods/run_host_tests.sh` for inspecting private, already-extracted mods without storing their packages in the repository.
- Implement upstream `HOOK_BEFORE_PHYS_STEP` output semantics for ground, air, water, and hanging physics. The hook receives Mario state, `STEP_TYPE_*`, and the step argument; the first valid integer return skips native stepping and becomes its result. The matching step/result constants are exposed to Lua. Host coverage verifies each step kind, the air step argument, and first-output short circuit.
- Bind `perform_ground_step`, `perform_air_step`, and `perform_water_step` to the existing engine functions with upstream Lua argument and result shapes. The host fixture calls all three through Lua and checks the returned native step results and airborne step argument.
- Bind upstream `play_sound(soundBits, Vec3f)` to the existing native sound engine, and expose mutable engine-owned `gGlobalSoundSource` and `gDefaultSoundArgs` vectors. The host fixture verifies the sound ID and all three position coordinates received by the native function; the unmodified Achievements ZIP no longer reports `play_sound` as unsupported.
- Extend the existing iOS workflow for `metal-validation/*` pushes so it runs a full iPhoneOS arm64 compile-only job; its IPA packaging/upload step explicitly excludes those branches.
- Extend the asset-free Xcode failure summary to include only validated linker symbol identifiers, while keeping raw Xcode output runner-local.

The original sandbox, ZIP validation, Lua API boundaries, existing native gameplay, and DynOS inactive status remain unchanged.

## Validation

- `bash tests/run_linux_checks.sh`: pass, including the host mod runtime, ZIP importer, existing gameplay checks, world-region checks, and 28 Python regressions. Existing compiler warnings in vendored Lua and `mario_actions_airborne.c` remain.
- `bash tests/mods/run_zip_import_tests.sh`: pass.
- `ASAN_OPTIONS=detect_leaks=0 CC=clang MOD_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' bash tests/mods/run_host_tests.sh`: pass with AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer had to be disabled because the sandbox reports that ptrace-based leak detection is unavailable; leak detection is therefore not verified.
- `MOD_EXTERNAL_ROOT=/tmp/mission69-omm-probe bash tests/mods/run_host_tests.sh`: existing host tests pass; the OMM source probe is discovered, skips its 32-bit chunk, and fails on the missing `audio_sample_load` binding as described above.
- GitHub Actions `Metal Apple SDK validation` run 44446368613 passed both Linux checks and its `ios-metal-compile` job on the `xcode-27` runner. This validates the Metal backend/MSL toolchain but does not compile the full SM64 iOS app for arm64.
- `Native iOS Metal IPA` run 44447280394 passed its Linux gate and SDL2/Metal validation, then failed at the full-app arm64 link with undefined symbols. Its diagnostics intentionally withheld symbol names; the updated sanitizer in this checkpoint will report only symbol identifiers on the next run. No IPA was packaged.
- Physical iPhone behavior has not been tested.

## M1 gaps and next work

M1 is not complete for the possession candidate because its source is unavailable here. OMM is conclusively incompatible at startup due to missing `audio_sample_load`; after audio, the next work must be driven by real failures in hook registration, object spawning, physics, collision, HUD/camera, and asset activation. Before any claim of OMM gameplay compatibility, run the exact untouched release ZIP through the app importer, preserve its assets, and exercise Mario movement and Cappy capture against the engine. DynOS decode/activation and Metal actor rendering are a separate mandatory milestone, not covered by this audit or the bytecode fix.
