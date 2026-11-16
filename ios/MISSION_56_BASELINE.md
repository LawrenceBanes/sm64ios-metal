# Mission 56 baseline audit

## Repository and branch

- Working branch: `mission-56-connected-world`
- Starting commit: `91006a884b22a4d4303e249f3f85c113f497a4af` (Mission 55.7 landing)
- Starting commit parent: `c456f2fb5ea5d4400cdc3d851c29e140a0cfc2c2`
- The live `ios` branch was read-checked at the starting commit. This mission uses an isolated worktree and does not update that branch.
- The primary checkout remains on `mission-55-6-codemagic`.

## Audited Mission 55.4 implementation

Mission 55.4's report and baseline files are absent from the current `ios` tree. The current source and `ios/PROGRESS.md` show the implemented test extension:

- `IOS_CONNECTED_WORLD_TEST` defaults to `0`; `IOS_HYRULE_WORLD` also defaults to `0`.
- Castle Grounds remains one level, one `AREA(1, ...)`, one `TERRAIN(castle_grounds_seg7_collision_level)`, and the stock `lvl_init_or_update` loop.
- The west-side trail reaches beyond the stock ±8192 collision boundary. The connected flag opts into a 64×64 collision partition covering the signed-16-bit coordinate range; expanded builds allocate 8,192 surfaces and 32,768 surface nodes.
- A small vertex-colored clearing is rendered through one extra display list. Its collision joins the existing corridor, but the current clearing is mostly flat and only covers the narrow opening.
- The opt-in branch opens two corridor wall triangles to connect the corridor to the clearing. With the feature disabled, the original Castle collision counts and geometry remain selected.
- The feature does not load Kokiri or Hyrule into the Castle area. There is no region streaming or per-region environmental blending.

## Relevant current files

- `levels/castle_grounds/script.c`, `areas/1/geo.inc.c`, and `leveldata.c` register the Castle area, rendering, and opt-in geometry.
- `areas/1/collision.inc.c` contains the stock Castle collision and conditional gateway/clearing collision.
- `src/engine/surface_partition.h`, `surface_load.c`, and `surface_collision.c` provide opt-in expanded bounds and partitioning.
- `Makefile`, `Makefile.split`, and `tests/run_linux_checks.sh` carry the build-mode invalidation and host regressions.
- `ios/HYRULE_IMPORTER.md`, `ios/KOKIRI_FOREST_AUDIT.md`, and `ios/PROGRESS.md` document the importer state and its limitations. Kokiri is not runtime-integrated.

## Baseline validation status

Mission 55.7's prior local report records passing Linux regressions and host syntax checks for connected collision/level sources. It also records that a full Linux link is blocked by the unchanged GL backend callback type mismatch. This Mission 56 branch has not run tests yet; results will be recorded in `MISSION_56_REPORT.md`. Apple Metal compilation and physical-device play remain unverified and are outside this mission's build scope.
