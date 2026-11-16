# Mission 57 — Connected-world engine foundation

Starting branch: `mission-57-engine`, isolated worktree `/home/larry/Projects/sm64ios-mission57`, based on Mission 56 commit `a6dd1074`. Engine implementation commit: `8f8d93f4`. This branch has not been pushed or merged. No GitHub Actions or Codemagic run was started.

## Phase A — Audit

**PASS (source audit).** See `MISSION_57_ENGINE_AUDIT.md` for call sites and unchanged limits. The main constraints are `s16` collision coordinates, a dense 64×64 opt-in partition, fixed legacy surface pools, a 240-object default pool, and a 20,000-unit Castle camera far plane. The connected forest is still embedded in the Castle collision stream. No device performance baseline exists.

## Phase B — Coordinate and collision safety

**PASS (host tests).** Added signed-64-bit region origins with checked conversion into the active `s16` physics window. Connected-world floor/ceiling/wall queries now reject coordinates outside the window before the legacy cast; the stock build retains its original semantics. The legacy collision loader rejects invalid triangle indices, oversized transformed-object vertex counts, and exhausted surface/node pools. A surface requiring more nodes than remain is rejected before any of its cells are linked. The area diagnostic includes the rejection count.

The original movement/acceleration, BLJ, swimming, flying, climbing, and wall equations were not changed. A real origin rebase is not implemented; this phase does **not** make live gameplay possible beyond `[-32768,32768)` local X/Z coordinates.

## Phase C — Region ownership and lifecycle

**PASS (headless collision test); PARTIAL (game integration).** `src/engine/world_region.[ch]` provides stable IDs, origins, explicit vertex/triangle counts, world bounds, neighbor IDs, owner tags, immutable render-list pointers, registration, activation, deactivation, and reset. Registration validates all coordinates, indices, surface types, triangle area, and a bounded node budget before modifying the active partition. Each region owns its `Surface` and `SurfaceNode` storage; repeated activation reuses the same pointers. Deactivation unlinks nodes, while storage remains allocated until area reset so stale Mario floor pointers cannot immediately dangle.

`area_update_objects` asks the manager to preactivate nearby regions before stock object/Mario updates. It uses current velocity/forward speed (including negative BLJ speed), a two-tick displacement allowance, and declared neighbors. Distant regions deactivate with hysteresis. This is deterministic and synchronous; there is no background thread or runtime GPU resource release.

The current authored forest remains in the Castle's original collision array. No new region descriptor is registered by the shipping Castle level yet. Actor ownership is metadata only; owned actors are not streamed. The manager is an integration API and host-validated collision substrate, not a completed kingdom streaming system.

## Phase D — Rendering and streaming

**PARTIAL.** The Castle geo layout has one opt-in generated-display-list hook for future registered region lists. It emits active opaque lists and conservatively culls by camera-to-region-AABB distance. Existing Mission 56 display lists still render as before. Registered display lists must already use area-local coordinates matching the collision transform; the geo hook does not apply the region origin. There is no full frustum test, texture-budget policy, transparent material path for registered lists, GPU buffer residency manager, or Metal performance measurement. Collision deactivation does not free memory; it is bounded by 16 registered regions, 8192 surfaces and 32768 nodes per region. These caps are safety limits, not an assertion that all maximum-size regions would fit an iPhone memory budget.

## Phase E — Validation

**PASS (Linux):** `CC=clang tests/run_linux_checks.sh`. This includes the existing importer, Castle connected-world geometry/winding/alignment, stock/Hyrule/connected build-mode checks, gameplay feature guards, host tools, and new region tests. The new headless test calls SM64's actual `find_floor` across two separately registered surfaces, verifies both sides of a seam, rejects wrapped coordinates and malformed triangles, checks reversed winding, exercises 100 activate/deactivate cycles with stable surface pointers, and checks fast negative movement and neighbor preactivation. A second test exercises the actual legacy loader's index/pool guards with tiny arenas.

**PASS (syntax):** Clang syntax checks for changed engine/game C units in connected, Hyrule-only, and ordinary configurations. **PASS (static analysis):** Clang analyzer on the new region module. **PASS (sanitizers):** AddressSanitizer/UndefinedBehaviorSanitizer region test with leak detection disabled because LeakSanitizer cannot run under this host's ptrace restriction. **UNVERIFIED:** complete host game link, Apple SDK/Metal compilation, IPA packaging, frame timing, memory pressure, and iPhone traversal. The isolated worktree has neither `baserom.us.z64` nor extracted Castle texture sources; this Linux host has no `xcodebuild` or `xcrun`. Existing ROM-derived assets were not committed.

## Remaining risks and next step

1. Register one genuinely authored or imported region through a separate area catalog; compare its visible display list and real `find_floor` results at the same seam on device. The Castle collision/geometry files should not need another region-specific edit.
2. Define a safe coordinated origin-rebase transaction before exceeding the local `s16` window. It must include player, camera, objects, kart, portals, dynamic platforms, and display-list transforms. World metadata alone is insufficient.
3. Add actor ownership and a bounded GPU resource policy after measuring iPhone CPU/GPU frame time and memory. Keep collision active before high-speed player movement and do not retire the surface Mario is using.
4. Validate on an iPhone before claiming seamless gameplay, camera continuity, or stable Metal rendering for registered regions.
