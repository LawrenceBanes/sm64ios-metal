# Mission 56 report — Connected World Expansion

## Result

**PASS — Linux collision and source validation.** The opt-in Castle forest now includes a raised meadow, main trail, walkable west loop, low-poly tree boundary, stone landmark, and an open rising connector aimed toward a future forest region. It remains part of Castle Grounds' existing active area and stock gameplay loop.

**PARTIAL — Architecture.** Static render/collision modules can be attached through a compile-time registry and the existing collision stream. There is no dynamic region residency, independent culling, environment blending, Kokiri import, or seamless Hyrule attachment yet.

**UNVERIFIED — iOS Metal and device gameplay.** This mission did not require an immediate iOS build. No Apple SDK or iPhone rendering/physics claim is made.

## Changes

- Added an opt-in connected-world baseline audit and architecture document.
- Expanded `connected_forest.inc.c` from the small flat patch into vertex-colored clearing/meadow/trails, four crossed-plane pines, and a stone landmark.
- Added `connected_world/registry.inc.c` and `north_transition.inc.c`; Castle geometry now calls one connected-world root display list.
- Added shared collision vertices and surfaces for the sloped meadow, main route, loop, edge walls, and open north trail. The connected collision declaration is updated to 528 vertices; the connected corridor/forest `SURFACE_DEFAULT` group is 48 triangles.
- Updated the previous gateway regression to expand the real feature-condition branches and included collision fragments rather than parsing inactive `#if` text as active geometry.
- Added `ios/test_connected_world.py` and wired it into `tests/run_linux_checks.sh`.
- Left Mario movement, BLJ, level update, camera, input, and the ordinary Castle collision/render branches unchanged.

## Validation

- `CC=clang tests/run_linux_checks.sh` — **PASS**. Existing Linux regression, host syntax, importer, gameplay, and tooling checks passed. New connected-world suite: 8 tests passed; existing gateway suite: 8 tests passed.
- Connected collision source compiled in stock, Hyrule-only, connected-only, and combined Hyrule/connected modes as part of the gateway C syntax test.
- Render registry and geometry compiled in a Linux host syntax check.
- Source tests confirmed 490 stock Castle vertices, 506 Hyrule-only vertices, and 528 connected-mode vertices; declared collision group counts and all triangle indices match their streams.
- Sampled route tests cover the Castle approach, clearing, raised meadow, loop branch, north corridor, and return route. New floor faces have positive Y winding; meadow walls face inward; rendered floors align with collision elevations.
- `clang-tidy` `clang-analyzer-*` on the connected registry — **PASS** using an explicit local config. The installed clang-tidy cannot parse the repository's `.clang-tidy` file without an override.
- `git diff --check` — **PASS**.
- Full `leveldata.c` syntax compilation was attempted and stopped at the absent generated asset `levels/castle_grounds/0.rgba16.inc.c`. The isolated collision, geometry registry, and scripts checks do not require those ROM-derived texture includes.
- No Actions or Codemagic build was started. `xcodebuild` and `xcrun` are unavailable in this CachyOS environment.

## Limits and next step

The open north endpoint is only a future attachment seam. No Kokiri/Hyrule region is resident in this area, and the player can leave the current collision at that endpoint. The current single static collision array uses signed-16-bit positions, a 64 by 64 partition, and fixed shared surface/node pools. The added geometry is small (70 triangles and 109 render vertices), but device frame time and camera appearance remain unmeasured. The next practical expansion is to attach a small, independently checked Kokiri-side receiving module with identical seam coordinates, then validate the combined landscape on an iPhone before adding region streaming.
