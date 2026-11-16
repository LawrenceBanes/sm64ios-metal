# Mission 55.4 baseline — Operation: The First Footstep

**Verified before edits:** branch `ios`, HEAD `a7d9de10011caaaa0d3a553c69129088943b2d36`, clean worktree.

## Runtime and build state

- `IOS_HYRULE_WORLD` defaults to `0`. When enabled it widens SM64's collision partition to 64×64 cells across the signed-16 coordinate range and raises the static-surface/node capacities. The ordinary 16×16 grid and pools remain the default.
- Castle Grounds is one SM64 level and area using the standard Mario object, `lvl_init_or_update`, `castle_grounds_seg7_collision_level`, and its normal geo display-list tree.
- The opt-in forest gateway is already authored in that Castle Grounds area, with a collision/render strip from X `-7420` to `-11100`, Z `720..2200`, floor Y `260`. Its endpoint has `bhvFadingWarp` to the separate Hyrule level; this is not continuous traversal.
- Original Castle Grounds collision has 490 vertices. The Hyrule option adds 16 corridor vertices and 18 corridor triangles. The area collision loader receives one `TERRAIN` stream.
- The Castle camera frustum uses near/far values 100/20,000. The corridor coordinates fit the opt-in signed-16 collision range.
- Importer transforms default to identity and have no selected or device-validated region placement. This mission's proposed test clearing will be deterministic SM64 geometry, not represented as imported OoT content.

## Placement evidence

The existing entrance is a viable first join: host-side interpolation over the original Castle collision finds Y=260 at `(-7400, 1460)`, while the appended trail collision is Y=260 from X `-7420` outward. The corridor runs along negative X and is 1,480 units wide in Z. This provides an already-tested, walkable approach without moving original Castle geometry. The new test branch will leave the north (positive-Z) side of this corridor before the Hyrule warp, keeping the legacy warp path distinct.

The camera far plane can cover this local extension. The large corridor width is enough to investigate Mario traversal; kart/Yoshi clearance has not been physically verified on device. Player and camera continuity are expected from remaining in the same level/area and using the stock update loop, but require runtime verification.

## Files and behaviors inspected

- `levels/castle_grounds/script.c`: one area, a single `TERRAIN`, and the existing optional gateway warp subscript.
- `levels/castle_grounds/areas/1/geo.inc.c`: Castle and gateway display lists are submitted under the same area camera node.
- `levels/castle_grounds/areas/1/collision.inc.c`: original and opt-in gateway vertices/triangles share one collision array.
- `levels/castle_grounds/areas/1/forest_gateway.inc.c`: deterministic vertex-colored gateway and trail mesh.
- `src/engine/surface_partition.h`, `src/engine/surface_load.c`, `src/engine/surface_collision.c`: expanded opt-in query grid and surface pools.
- `tools/hyrule_importer.py`: optional yaw/translation baking exists, but no imported region is currently registered in Castle Grounds.
- Kart state is held in Mario/game-side state and validated against area ownership; the seamless test will not load another level or area. Yoshi/kart traversal is secondary to Mario's initial proof.

## Validation limits

Mission 55.3 host checks passed, including importer, collision-grid, gateway, Linux, and host-object checks. The CachyOS host has no Xcode/iPhoneOS SDK/Metal compiler; there is no IPA or device validation. This baseline does not claim visible forest rendering or physical traversal.
