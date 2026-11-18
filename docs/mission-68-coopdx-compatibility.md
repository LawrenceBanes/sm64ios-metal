# Mission 68: CoopDX Compatibility and DynOS Milestones

## Status

This worktree is based on `a173f28b6130e58982c6d427f2bbbb7fabbc238e` on the isolated `mission-68` branch. It is not integrated with Mission 67. The code currently provides an initial Lua compatibility core and a bounded ZIP importer. It does **not** run general existing SM64CoopDX mods unchanged yet.

DynOS is a major compatibility milestone and a release gate. ZIP recognition and retention are only the import stage; packs are intentionally shown as inactive until the native engine can load and render their content.

## Upstream behavior used as reference

The upstream [Lua mod guide](https://github.com/coop-deluxe/sm64coopdx/blob/main/docs/lua/lua.md) describes scripts loaded from a mod folder and DynOS actors stored under that mod's `actors/` directory. The [hook guide](https://github.com/coop-deluxe/sm64coopdx/blob/main/docs/lua/guides/hooks.md) defines behavior overrides, event hooks, and custom Mario actions. [ModFS](https://github.com/coop-deluxe/sm64coopdx/blob/main/docs/lua/guides/modfs.md) is a sandboxed per-mod filesystem with bounded storage. Upstream [`mod.c`](https://github.com/coop-deluxe/sm64coopdx/blob/main/src/pc/mods/mod.c) sorts script files, recursively discovers them for module loading, and separately indexes actor, texture, behavior, sound, and level assets. [`smlua.c`](https://github.com/coop-deluxe/sm64coopdx/blob/main/src/pc/lua/smlua.c) auto-runs only root-level scripts, while [`smlua_require.c`](https://github.com/coop-deluxe/sm64coopdx/blob/main/src/pc/lua/smlua_require.c) resolves nested modules relative to the current script. Upstream ZIP type detection in [`mod_import.c`](https://github.com/coop-deluxe/sm64coopdx/blob/main/src/pc/mods/mod_import.c) treats `.lua`/`.luac` as Lua content and `.tex`/`.png`/`.bin` as DynOS content. The DynOS API surface in [`dynos.c.h`](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos.c.h) includes actor, texture, model-pool, collision, level, and behavior registration; its pack startup flow is represented by [`dynos_gfx_init.cpp`](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_gfx_init.cpp).

The importer in this branch accepts both source and Lua 5.3 bytecode, validates paths and archive bounds, stages extraction, checks CRCs, and keeps Lua package files and actor assets together. It recognizes the same DynOS extensions as upstream. Upstream's pack generator also scans mod `actors/`, `levels/`, `data/`, and texture folders, so precompiled `.bin`/`.tex` files are only part of the content path. This branch currently preserves those files but does not generate or decode them into game resources.

## Compatibility matrix

| CoopDX surface | Status here | Current behavior and boundary |
| --- | --- | --- |
| Mod discovery and deterministic startup | Adapted | Discovers sandbox folders and sorts by directory name. Auto-runs only root-level `.lua`/`.luac` files in lexical path order; nested scripts remain available to `require()`. Directory mods require root-level `main.lua`. A failing root startup script tears down that mod's registrations and disables it. It does not yet parse upstream name/category/incompatibility metadata or resolve dependencies. |
| Lua source / bytecode | Adapted | Embedded PUC Lua 5.3.5; source and compatible Lua 5.3 bytecode are accepted. LuaJIT sources are vendored for evaluation but are not part of the game build. |
| Per-mod globals and `require` | Adapted | Per-mod environment and module cache; slash paths resolve relative to the script that calls `require`, including callbacks. `.` and `..` are normalized while preventing escape above the mod root. No arbitrary path, package loader, or native module access. |
| `hook_event` | Partial | Supports `HOOK_UPDATE`, `HOOK_BEFORE_MARIO_UPDATE`, `HOOK_MARIO_UPDATE`, `HOOK_ON_SET_MARIO_ACTION`, `HOOK_BEFORE_SET_MARIO_ACTION`, `HOOK_ON_HUD_RENDER`, `HOOK_ON_LEVEL_INIT`, and `HOOK_ON_EXIT`. Other CoopDX event IDs throw an explicit unsupported-capability error. |
| `hook_mario_action` | Partial | Custom actions use a reserved native action-ID range and can supply `every_frame` and `gravity` callbacks. IDs are owned per mod and released on unload. Broader Mario physics interception is absent. |
| Mario state | Partial | Bounded fields and vectors are exposed via checked userdata. Unsupported fields throw instead of returning dummy values. This is one local player only. |
| Controller input | Partial | Read-only controller fields are available through the Mario state adapter. Touch/controller translation remains in the native input path. CoopDX's full controller arrays and multiplayer player indexing are absent. |
| `hook_behavior` | Partial | New local scripted behaviors can be registered and attached to locally spawned objects. Replacing native behavior IDs is rejected. No synchronization or complete behavior command API. |
| Object spawning and lifecycle | Partial | Local objects use native spawn helpers, engine behavior lists, and owner cleanup. Only native model IDs are accepted. DynOS IDs are rejected. Object properties outside the allowlist fail explicitly. |
| Animation registration | Unsupported | Native animation playback by existing ID is available; importing or registering custom animation data is not. |
| Model registration | Unsupported | Existing native model IDs work. Dynamic model tables, actor parsing, custom geometry, and model symbol lookup are not implemented. |
| Texture registration / overrides | Unsupported | No `.tex` or actor texture decoder/registration path. The ZIP importer retains files only. |
| Sound registration / playback APIs | Unsupported | No custom sound bank decoder or CoopDX sound API bindings. |
| Character Select | Unsupported | No CoopDX character registry, character metadata, model/animation binding, or selection screen integration. |
| HUD and rendering | Partial / unsupported | `HOOK_ON_HUD_RENDER` can run. CoopDX DJUI drawing, text, textures, and render-state APIs are not bound. |
| ModFS and persisted per-mod data | Unsupported | No `mod_fs_*` or storage bindings. `io`, `os`, `package`, and `debug` libraries are excluded; `dofile` and `loadfile` are disabled. |
| Dependency/load ordering | Partial | Stable lexical ordering only. Dependency declarations, version constraints, conflict resolution, and upstream enable semantics are not implemented. |
| Multiplayer sync / networking | Unsupported | Network tables, synchronized objects, remote players, lobby integration, and cross-client asset negotiation are unavailable. |
| ZIP import | Partial | Sandboxed extraction with path, count, size, ratio, collision, symlink, and CRC checks. No automatic dependency install, signature validation, or upstream metadata processing. |

## DynOS: major compatibility milestone

DynOS must be completed as a cross-cutting engine subsystem. It is not a final polish item and blocks existing character, object, and model-heavy mods from being considered compatible.

### Current state

- **D0 — Import and inventory: partial.** ZIP archives are inspected and safely extracted. Lua-bearing archives preserve `actors/` and asset files under the mod folder. DynOS-only archives are preserved under the sandbox DynOS pack folder; the iOS manager inventories `.bin`, texture, collision, level, behavior, and audio files and marks each pack inactive. This is inventory only; it does not validate DynOS binary structure or activate resources.
- **D1 — DynOS discovery and format validation: not implemented.** There is no parser/validator for upstream actor trees, level packs, behavior data, compiled model `.bin` data, texture `.tex` data, or source actor descriptions. No malformed-asset diagnostics beyond ZIP validation. Upstream activation covers `.bin`, `.col`, `.lvl`, `.bhv`, and `.tex`; ZIP recognition alone does not activate any of those resources.
- **D2 — Actor/model registration: not implemented.** The engine has no dynamic model registry or CoopDX-compatible model-ID allocation. The Lua spawn adapter explicitly rejects IDs outside the native model table.
- **D3 — Metal geometry path: not implemented.** No DynOS geometry/display-list conversion or dynamic geometry upload path is connected to the native Metal renderer. DynOS graphics commands, material state, culling, translucency, and texture formats are not translated.
- **D4 — Texture pipeline: not implemented.** No `.tex` decoder, PNG conversion/registration, palette management, or texture ownership/unload integration.
- **D5 — Animation and collision: not implemented.** No DynOS skeleton/animation loading, actor collision registration, or dynamic collision teardown.
- **D6 — Lifecycle and compatibility validation: not implemented.** No pack activation UI, enable/disable cleanup, memory accounting, or tests against actual upstream DynOS packs.

### Completion gates

DynOS can be called supported only after all of the following pass:

1. Parse representative existing upstream `.bin` and `.tex` packs with explicit format/version and bounds validation.
2. Register dynamic actor models and textures through engine-owned resource handles.
3. Render those actors in the iOS Metal command pipeline, including the supported geometry and material cases used by the compatibility pack set.
4. Load and unload a pack repeatedly without leaked model IDs, texture memory, geometry buffers, or stale object references.
5. Load DynOS actors from both `dynos/packs/<pack>` and `<mod>/actors`, matching upstream discovery behavior.
6. Pass host parser/registration/render-command tests and iOS compilation. Verify actual visual rendering on an iPhone before claiming device compatibility.
7. Test at least one unmodified character pack and one non-character actor/model pack. A synthetic Lua script is not DynOS evidence.

## Lua runtime and safety limits

The build uses embedded Lua 5.3.5 as the interpreter. The shared Lua state has a 32 MiB allocator limit across loaded mods; each callback is bounded to 50,000 Lua VM instructions. Lua coroutines inherit the bounded count hook from Lua 5.3 threads, and a regression fixture exercises an infinite coroutine. Script callbacks are isolated; a failing callback is disabled and logged. The approved Lua libraries are base, table, string, math, coroutine, and UTF-8, with filesystem/native loading libraries withheld.

LuaJIT 2.1 is present under `vendor/luajit` for evaluation but is not compiled, linked, or selectable at runtime. It is useful as an optional performance backend, and its [platform status](https://luajit.org/status.html) lists iOS and ARM64 among supported targets. Its [compatibility documentation](https://luajit.org/extensions.html) describes Lua 5.1 API/ABI compatibility with selected later-language extensions, not a drop-in Lua 5.3 runtime: in particular, it does not provide Lua 5.2's `_ENV` model, and its bytecode cannot load PUC Lua bytecode. The common CoopDX-facing bindings should stay runtime-independent, while each backend must pass the same source, bytecode, sandbox, coroutine, and hook tests. If the JIT backend cannot load an imported bytecode script, the runtime must report that compatibility reason and use source when present; it must not silently misinterpret the bytecode. Apple's [JIT guidance](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon) documents `MAP_JIT` and (for hardened runtime) the JIT entitlement, plus callback allow-list requirements; it also says `pthread_jit_write_protect_np()` is unavailable on iOS. The signing path and actual iPhone execution remain unverified, so JIT remains optional and must never be required to load or play ordinary mods.

The current execution budget counts Lua VM instructions; it is not a wall-clock preemption mechanism for long-running C library calls. That is a remaining sandbox hardening item before importing arbitrary community scripts is considered robust.

## Package limits

ZIP import is bounded to 128 MiB compressed archive, 1,024 entries, 64 MiB per expanded file, a 250:1 compression ratio, and 256 MiB total expanded data. Paths must be relative and normalized; symlinks and case-insensitive destination collisions are rejected. Extraction uses a private staging directory and validates CRC before installation. The importer does not establish trust in the Lua content: Lua scripts remain code, but cannot access general host filesystem APIs or load native libraries.

## Synthetic regression coverage

`tests/mods/run_host_tests.sh` covers sorted discovery, slash-based and callback-relative Lua `require`, per-mod module-cache isolation, source and PUC Lua 5.3 bytecode loading, hook dispatch, Mario/controller bindings, custom actions and gravity, object behavior/spawning, callback errors, startup-failure cleanup, enable/disable, multiple mods, coroutine instruction limits, and read-only DynOS asset inventory. `tests/mods/run_zip_import_tests.sh` covers mixed Lua/DynOS import, DynOS-only classification, traversal rejection, and path collision rejection. These are framework tests, not proof that existing third-party mods work.

## Handoff to Astra

Review this branch as an incomplete compatibility foundation. Keep it isolated from Mission 67. DynOS D1–D6, broader CoopDX API parity, real unmodified mod packages, full dependency semantics, and iOS toolchain/device validation remain integration blockers. Do not merge or distribute a build based on this status.
