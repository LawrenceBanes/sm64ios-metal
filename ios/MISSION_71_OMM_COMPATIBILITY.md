# Mission 71 — OMM Rebirth v1.4 compatibility audit

## Target identity and archive contents

The local target archive identifies itself in `omm-coop/main.lua` as **OMM Rebirth 1.4**, by PeachyPeach. Its original public source is [PeachyPeachSM64/coopdx-mods-omm-rebirth](https://github.com/PeachyPeachSM64/coopdx-mods-omm-rebirth), and the matching upstream release is [OMM Rebirth v1.4](https://github.com/PeachyPeachSM64/coopdx-mods-omm-rebirth/releases/tag/v1.4). This is the CoopDX Rebirth package, not the `sm64ex-omm` distribution or a later mod release.

The archive has 198 entries (2.5 MB compressed, 3.0 MB uncompressed): 89 Lua files, 13 DynOS `.bin` actor bundles, one `.bhv` behavior, 80 `.tex` textures, and 10 OGG samples. Lua scripts are shipped as paired 32-bit and 64-bit Lua 5.3 bytecode; this app correctly executes the 64-bit files and reports/skips the 32-bit copies. The package header describes Odyssey/Cappy moves, custom star/death sequences, improved power-ups, an Odyssey HUD, star/settings menus, non-stop mode, and a custom camera. The archive contains no license or README file, so the original package and assets remain local and are not redistributed.

The upstream mod page describes unique Odyssey/Cappy moves, custom animations and star/death dances, improved power-ups, non-stop mode, an Odyssey HUD with level-star display and radar, and a custom camera. These are audit targets, not claims of compatibility.

## Feature matrix at checkpoint `340b14ae` plus current Mission 71 work

| Subsystem | Status | Evidence and remaining gap |
|---|---|---|
| ZIP import and 64-bit Lua startup | Implemented and host verified | Production ZIP importer installs the unmodified archive; the 64-bit Lua 5.3.5 scripts load. 32-bit bytecode is skipped on this 64-bit runtime. Startup remains host-only evidence. |
| Event hooks and OMM Lua initialization | Implemented; partially verified | Startup registers callbacks and the local test dispatches update/Mario/HUD events. The local network-player view now includes the current course field expected by OMM. HUD-behind callbacks run through the host runtime; a world callback still reaches an unimplemented Bowling Ball Spawner behavior lookup. |
| Custom actions and moveset | Partially supported; host callback verified | The host test reads OMM's `ACT_OMM_CAPPY_THROW_GROUND` from its per-mod environment, seeds controller input, and dispatches the real registered action callback. It consumes the input and transitions Mario's action state (`0x03000880` in this fixture run). This validates a real Lua-to-native action callback and controller write, but not a complete Cappy launch, bounce/vault transitions, or full moveset gameplay. Multiplayer synchronization callbacks are inactive offline. |
| Peach model | Host verified through production path | `omm_peachy_geo` loads, registers as a dynamic extended model, attaches to the local Mario object, traverses production `geo_process_object()` and produces display-list submissions. Host traversal is not a Metal pixel or iPhone result. |
| Other OMM actor models | Partially supported | Six sparkle actor graphs register. Lookups for the four Tiara variants and opaque/translucent OMM star models fail GeoLayout validation with `unbalanced geolayout end`; calls resolve to the native error-model ID. |
| OMM custom Lua animations | Registered and host traversal verified | Ten original animation tables load. The Mission 71 host test applies the original `anim_mario_omm_cappy_throw` record, advances it through Peach's production GeoLayout animated parts, confirms animated transform values change, and confirms native display-list submissions. This does not yet verify action-to-animation selection, all animation transitions, or device output. |
| DynOS `.bhv` | Partially supported | `bhvOmmCappy` is recognized as an active bounded behavior using the supported command subset. Unsupported opcodes deactivate the affected behavior; full Cappy object lifecycle and interactions still need gameplay tests. |
| Textures and materials | Partially supported; host decoded | PNG-backed actor textures and all 80 standalone `.tex` resources in this archive decode under the 32 MiB per-mod decoded texture cap. Exact Metal material appearance has not been verified on iPhone. |
| HUD and menus | Partially supported; authentic host HUD submissions verified | Original `HOOK_ON_HUD_RENDER_BEHIND` callback dispatches through the production Lua hook path and submits 13 texture draw commands in the bounded host harness. LuaJIT-compatible numeric-to-integer conversion is implemented for fractional HUD color/alpha values used by the original HUD. These are host command submissions, not GPU pixels. Radar accuracy, pause/star lists, touch controls, and `showStarNumber`/extended-pause rendering remain unverified or unavailable. |
| Audio | API present; OMM playback unverified | `audio_sample_load` / `audio_sample_play` bindings and owned sample cleanup exist; the archive contains ten OGG samples. The current test does not assert sample decode, playback, or cleanup against these ten files. |
| Camera | Partially supported | Lua camera-state and mode bindings exist. The build lacks CoopDX `BETTERCAMERA`; OMM's camera configuration queries warn that the options are unavailable. |
| Collision compatibility flags | Unavailable with explicit fallback | `gLevelValues.fixCollisionBugs*` fields have no native collision-pipeline adapter. The runtime exposes nil and warns once so scripts can take their optional-feature path; it does not emulate CoopDX collision patches. |
| Settings and storage | Partially supported | Mod-local persistent storage and settings/menu callbacks exist. CoopDX sync tables are local-only; values do not synchronize with other players. |
| Character selection / Peach-specific abilities | Missing or unverified | This archive uses Mario-action and model-selection behavior, not the separate Character Select plugin. No independent Peach ability implementation has been inferred from this package. |
| Multiplayer | Explicit architectural limitation | Packet receive is inactive and sync tables are mod-local. Single-player behavior can proceed without this; networked OMM behavior is not supported. |

## Reproducible host checks

The authentic package and extracted data stay under `/home/larry/Downloads` and `/tmp`; neither is tracked. The test executes the production ZIP loader, Lua runtime, model registry, and production GeoLayout traversal:

```sh
MOD_EXTERNAL_ZIP=/home/larry/Downloads/omm-coop.zip \
MOD_TEST_EXPECT_OMM_MODELS=1 \
MOD_TEST_DYNOS_ACTOR_ZIP=/home/larry/Downloads/omm-coop.zip \
MOD_TEST_DYNOS_ACTOR_ENTRY=omm-coop/actors/omm_peachy_geo.bin \
MOD_TEST_DYNOS_ACTOR_NAME=omm_peachy_geo \
MOD_TEST_DYNOS_GRAPH_RENDER=1 \
MOD_TEST_OMM_ANIMATION=1 \
MOD_TEST_OMM_ACTIONS=1 \
MOD_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
ASAN_OPTIONS=detect_leaks=0 CC=clang bash tests/mods/run_host_tests.sh
```

The targeted authentic OMM run passes host ASan/UBSan, executes Peach GeoLayout traversal (23 display-list appends, 15 callbacks, 41 transform nodes), verifies changing transforms from OMM's original Cappy-throw animation data, and dispatches the original ground-throw action callback. The callback consumes seeded controller input and changes Mario action state. With the OMM HUD test state set to Bob-omb Battlefield, the unmodified HUD callback submits 13 texture commands in the behind pass and no texture commands in the front pass. All 80 archive `.tex` resources decode under the revised bounded texture budget. The full Linux suite passes locally. The current code checkpoint is `d7c9561aecc29cb8db020e79e2dd81d6d08eddc9`; its compile-only Apple workflow is [44642628546](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44642628546) and is pending. Earlier Xcode 27 Metal validation, iPhoneOS arm64 compilation, IPA structural validation, and private unsigned IPA upload pass for commit `91f72e3877d788047383749bb457786ee8253e9d` in [workflow 44638359362](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44638359362). The [private unsigned IPA release](https://github.com/LawrenceBanes/sm64ios-private-builds/releases/tag/sm64-ios-unsigned-44638359362-1) contains no OMM ZIP or assets; IPA SHA-256 is `3b82930349b7042b46b5548ddd1ad3f6fd8dd2a51776fdc5593f69b39a13efa9`. These results do not verify a complete Cappy launch, Metal pixels, or physical iPhone operation.

## Next compatibility targets

1. Exercise original OMM action callbacks with deterministic local controller input and assert Mario-state transitions, including Cappy throw and bounce/vault paths.
2. Extend GeoLayout interpretation for the authentic Tiara/star actor graphs without bypassing validation.
3. Connect registered OMM animations to the action transitions used by the original scripts; manual animation selection remains a host probe.
4. Verify original sample decode/playback and complete owned-resource cleanup.
5. Implement or explicitly bound the Bowling Ball Spawner behavior and other gameplay callback requirements found by deeper execution.
6. Validate the integrated candidate in Apple CI, then have the user verify controls, visuals, and sound on iPhone.
