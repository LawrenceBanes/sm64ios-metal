# Connected-world architecture decision

## Decision

Use a **single active SM64 level/area with a hybrid static-world layer** for the first connected-world implementation. Keep the normal Mario object, `lvl_init_or_update`, camera, collision queries, controls, and Metal renderer. Represent each imported region as data with one explicit world transform; combine neighboring static geometry and collision into that same area. Keep all regions needed for the initial route resident. Add region visibility and resource unloading only after measurements show the combined route needs it.

This is the smallest architecture that can preserve one gameplay session and avoid warp/fade transitions. A region registry may describe transforms, bounds, source identity, and future visibility, but it must not own a second player simulation or introduce region-transition teleports.

## Current state versus target

| Concern | Current implementation | Target for continuous travel |
| --- | --- | --- |
| Player/update loop | One standard SM64 loop per level | One loop for Castle, connector, Kokiri, and Hyrule |
| Region entry | Paired `bhvFadingWarp` level transition | Ordinary movement through joined terrain |
| Static rendering | Castle area plus standalone Hyrule level Gfx | Region display lists composed into one active area's opaque/translucent lists |
| Collision | One area's terrain is loaded by SM64 | A single merged collision stream with transformed region vertices and one `TERRAIN` load |
| Coordinates | SM64 `s16`; imported OoT axes are Y-up, scale 1.0 | One explicit transform per region, validated consistently for vertices, collision, spawns, objects, and camera bounds |
| Streaming | None | None for first route; optional region visibility/residency later |
| Kart/Yoshi | Actors are area/level owned | Keep same actors and state because the level/area is not replaced |

## Coordinate and collision limits

The SM64 collision ABI stores each vertex coordinate in signed 16-bit values. It cannot represent an unbounded world. The Hyrule-enabled build now uses a 64-by-64 collision grid with 1,024-unit cells over the representable signed coordinate interval; the ordinary build remains on its original 16-by-16 grid. The opt-in surface and node pools are enlarged to 8,192 surfaces and 32,768 spatial nodes. This is a query-range accommodation, not region integration or proof that a combined world fits the pools.

Current local source bounds are:

- Castle connector: approximately X `-11,100`, Z `720..2,200`.
- Hyrule Field default room: X `-11,048..6,732`, Y `-1,095..1,407`, Z `-1,811..16,529`.
- Kokiri Forest default-room collision: 1,081 vertices and 1,692 triangles; scene bounds and final placement still require a validated selected setup.

Any region placement must keep transformed geometry and player positions in the `s16` representable range. Origin rebasing is not implemented. If a later world exceeds this range, the solution must change collision coordinate management coherently; merely shifting the render camera would break floor, wall, portal, kart, and object queries.

The host importer now accepts `--world-translation X Y Z` and
`--world-yaw-degrees ANGLE`. Translation is in SM64 units after scaling;
positive 90-degree yaw maps a region's local +Z direction to world +X. The
transform is baked into supported render vertices, collision vertices,
collision-bound metadata, and selected spawn/kart/return positions. Referenced
render vertices and collision/spawn coordinates are range-checked. This is a
conversion capability only: no Castle/Kokiri/Hyrule transforms have been
selected or applied to a runtime level, and scene actors/entrances are not yet
lowered into the level object list.

## Integration sequence

1. Finish resource closure for each selected scene setup. Keep unresolved segmented display-list pointers visible as omissions and do not invent their referents.
2. Inspect authentic geometry and collision together, then define region transforms from actual entrances, terrain heights, and headings. Do not infer adjacency from archive names or choose offsets solely to avoid overlap.
3. Generate transformed display-list vertices and a single combined `Collision` data stream. Validate triangle counts, index ranges, `s16` bounds, degenerate faces, and seams before registering it in Castle's existing area. Reuse `TERRAIN` and the stock surface queries.
4. Add the Kokiri and Hyrule Gfx lists to that area's existing opaque and translucent submission paths. Preserve Castle's models, materials, lighting, and level table. Static transforms should be baked at conversion time initially; this avoids a renderer rewrite and keeps collision aligned.
5. Place Mario at the original Castle spawn and walk through the connector. Keep the return route physically traversable. Do not add a warp trigger.
6. Profile total surfaces, node use, memory, draw submission, and frame times on the target device. Only then introduce region culling or resident-set changes, while retaining one SM64 gameplay session.

## Why not stream now

The current authentic subsets are small enough to investigate as a resident route, while importer completeness and spatial layout are not yet established. Streaming would add asynchronous ownership and collision-gap failure modes before there is evidence that memory or frame time requires it. The first world layer should therefore be static and bounded. Later regions can use the same region descriptor and world-transform convention, with a region visibility/residency policy added independently.

## Known blockers

- Hyrule Field has three unresolved segmented calls; Kokiri Forest has six.
- Kokiri's selected alternate setup, actor/object dependencies, water, and transition data are not yet represented by the native export.
- No validated Castle-to-Kokiri-to-Hyrule world transforms or collision seam tests exist yet.
- The current gateway changes levels and loses kart entity continuity.
- There is no iOS build or device evidence for the imported scene rendering.
