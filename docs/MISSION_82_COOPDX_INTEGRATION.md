# Mission 82 — CoopDX Mod Integration

## Architecture decision

Mission 82 builds on the mod runtime already integrated in `main`; it does not
replace the Fast3D-to-Metal renderer or create a second Lua/game-state runtime.
The UIKit Mods screen copies Files-picker ZIPs into app-managed storage, then
queues installation on the game thread. The bounded ZIP importer validates and
extracts transactionally under `Documents/mods/`; the Lua runtime discovers
installed packages in deterministic package-ID order and owns their scripts,
callbacks, objects, and decoded resources. Existing Metal model/texture
adapters remain the renderer boundary for supported DynOS resources.

The Leo iOS fork is useful as a platform behavior reference (Documents access,
touch/controller integration, and iOS packaging), but its app build selects
SDL/OpenGL/GLES. Its rendering backend is incompatible with our Metal ownership
and is not imported. Neither it nor the upstream repository has an explicit
license declaration in the inspected repository metadata/tree. No external
implementation code has been copied into this branch; source reuse remains
subject to a separate licensing review.

## Upstream behavior represented here

The upstream loader reads metadata from leading comments in `main.lua`,
including `name`, `category`, `description`, and `incompatible`. The current
runtime now parses those values as bounded text before Lua execution and
publishes them through both `gActiveMods` and the native Mods UI. The directory
name remains the stable package ID used for ownership and persistence.

CoopDX incompatibility declarations are whitespace-separated tokens: two mods
conflict when both declarations contain at least one identical token. Startup
resolves conflicts in sorted package-ID order and reports which package was
disabled; interactive enabling is rejected before queueing when it conflicts
with an active package. This is a deterministic single-player policy adapted
to this port's current auto-enable behavior.

The upstream feature set inspected here does not define a general declarative
mod dependency field in this header. Same-mod nested `require()` modules are
supported by the existing runtime and remain isolated to each mod. Cross-mod
module loading is not implied by `gActiveMods` metadata.

## Compatibility snapshot

| Area | Current implementation/evidence | Remaining limitation |
|---|---|---|
| ZIP import | Existing UIKit document picker and production ZIP importer; persistent app-sandbox install | No external iPhone import evidence in this mission checkpoint |
| Lua lifecycle | Lua 5.3.5, per-mod environment, bounded memory/instructions, startup and cleanup | CoopDX APIs requiring unavailable game subsystems still fail with diagnostics |
| `require()` | Nested modules within each imported mod; module cache and per-mod isolation | No cross-mod package dependency resolver |
| Mod metadata | Bounded leading-comment metadata, native UI labels, `gActiveMods`; conflict token handling | Full upstream Mod menu and network mod state are not reproduced |
| DynOS | Existing bounded `.bin`/`.tex` actor path and production GeoLayout-to-Metal traversal tests | Full DynOS material/animation/`.col`/GeoLayout opcode coverage remains incomplete |
| Audio | Existing sample decode/ownership APIs and native mixer adapter | Formats, channel behavior, and OMM-specific audio still need real-device verification |
| Multiplayer | Explicitly isolated from this mission | CoopNet and synchronized state are unsupported by scope |

The importer now recognizes one unambiguous nested Lua package root when an
archive also contains sibling samples or editor folders. It installs that
folder's content and reports that sibling entries were ignored. Archives with
multiple nested `main.lua` roots are rejected with an instruction to import
each package separately. A synthetic fixture verifies this path and its
reported omission behavior.

## Authentic package probes

- **SM64 Coop Achievements 2.1.0** — the original MIT ZIP passes production
  import, multi-file startup, event dispatch, persistence/menu checks, and
  disable/re-enable host tests. It requests `stars_1`, which is not currently
  available in the native texture lookup. This is partial, not full UI parity.
- **OMM Coop (`omm-coop.zip`, local only)** — the unmodified archive imports
  and starts under the host runtime. Its six startup graphs and Peach actor
  resolve in the authentic-model test path, including disable/re-enable. A HUD
  callback still errors, and GPU output/device behavior are not established.
- **Bowser Moveset v1.2 (local only)** — its ZIP layout exposed the nested-root
  importer defect; the corrected importer now installs its `Bowser Moveset`
  root while ignoring the sibling `Template Character` folder. Its Lua startup
  reaches event registration with the native Mario animation enum exposed. The
  package explicitly requires Character Select; without that subsystem, a
  later callback errors because the expected `bowsMoveset` global is absent.
  It is therefore imported but not playable in this port. The source archive
  has not been redistributed or committed.

## Test fixtures and provenance

The committed real package fixture `tests/mods/real/sm64coop-achievements-2.1.0.zip`
is MIT-licensed and exercises actual ZIP installation and Lua startup. The
local OMM archive at `/home/larry/Downloads/omm-coop.zip` is used only as a
non-redistributed test input when present; its contents are not added to Git.
Host startup success does not establish full gameplay compatibility or iPhone
rendering.

## Next integration work

1. Run the complete regression and sanitizer suite after metadata changes.
2. Exercise a nested-module package and an original DynOS character archive
   through the production importer, recording API calls and unsupported
   behavior rather than interpreting import as compatibility.
3. Close the largest repeated runtime blockers across those unmodified mods.
4. Run Xcode/iPhoneOS CI for the UIKit and C-runtime changes; physical device
   import and gameplay remain separate evidence.
