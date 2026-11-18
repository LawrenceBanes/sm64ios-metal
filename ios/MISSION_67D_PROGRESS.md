# Mission 67D progress

Worktree `/home/larry/Projects/sm64ios-mission67d`, branch
`mission-67d-o2r-compatibility`, starting HEAD
`400173391115b56543444377154dbdb8c36a8a30`. The 67B worktree and 67C are
separate. The archive and full inventory remain local.

## Reconnaissance checkpoint

Read the 67B handoff, scene runtime documentation, inventory reader and
binary metadata readers. Recounted the original catalog and inspected the
upstream scene, entrance, spawn, room and resource factory definitions.
All default scene roots have room and entrance metadata. The graph
represents header-local records, including repeated alternate variants.
There are 264 distinct raw exit IDs. The three unresolved room records
use 0xff on an absent side and require signed-field interpretation.

Next, implement an offline validator that preserves local slots and only
joins them against explicitly supplied, catalog-bound global rows and
destination headers. Verify with authored fixtures and the local catalog.

## Validation checkpoint

The format-neutral layout and row validators are in `tools/scene_metadata.py`.
The offline CLI in `tools/audit_o2r_entrances.py` reads the 67B catalog and a
separate, optional evidence map. It validates default layouts, rejects
ambiguous or mismatched rows, and distinguishes same-room and absent-side
records from room links.

The real catalog run validated 101 default layouts and 442 local entrance
tuples. Of 638 room records, 426 are between different rooms, 209 are
same-room, and three are one-sided. The ten-row layer-zero reference
comparison matched all 12 Kokiri default exit slots. The report covers
the full lookup chain and the remaining material, collision, residency,
spawn and trigger requirements. See `ios/MISSION_67D_COMPATIBILITY.md`.

The exact observed reference comparison stays in the ignored build directory.
The paths are `build/mission67d/kokiri-default-reference.json` and
`build/mission67d/exit-audit-reference.json`. No runtime map was installed.

## End of session

Validated implementation commit `d3d0f20731d3544f036996cc7101435ab07ca5bc`.
The next commit records this checkpoint only.

- `python3 tests/test_scene_entrance_metadata.py` passed eight authored
  fixture tests. `python3 tests/test_o2r_inventory.py` passed four tests.
- `CC=clang sh tests/run_linux_checks.sh` passed. The complete log is
  `build/mission67d/linux-checks.log`.
- The audit CLI passed against the real 67B catalog both without a mapping
  and with the local ten-row reference comparison. `git diff --check`
  and Python bytecode compilation passed.
- The first Kokiri slice still needs a verified setup and matching source
  table, draw-state and material support, scene collision ownership, safe
  spawns and boundary triggers, and an SM64 return path. No Apple or iPhone
  validation occurred. No CI run, push, IPA, or asset upload occurred.
