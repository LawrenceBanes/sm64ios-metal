# Connected world architecture

## Current design

`IOS_CONNECTED_WORLD_TEST=1` composes authored region modules into the existing Castle Grounds area. The default remains `0`; `IOS_HYRULE_WORLD` also remains opt-in and defaults to `0`.

Castle Grounds still has one `AREA(1)`, one `TERRAIN(castle_grounds_seg7_collision_level)`, and the normal `lvl_init_or_update` loop. Mario, collision queries, camera, inputs, BLJ behavior, objects, and level state all remain on the stock SM64 path. There is no region-triggered warp or gameplay update hook in the forest extension.

The rendering seam is a compile-time registry:

- `areas/1/connected_world/registry.inc.c` includes the current forest and north-transition modules and emits one root display list.
- `areas/1/geo.inc.c` references that root once, only in connected-test builds.
- Additional static render modules can be added to the registry without editing the Castle level script or adding another area.

Collision is aggregated into the existing Castle collision array:

- `areas/1/collision.inc.c` selects the connected vertex count and includes the regional vertex and surface fragments under the connected flag.
- Collision indices are global within the one `Collision` array. New regions must append vertices and use a matching, explicitly counted `COL_TRI_INIT` group.
- The corridor, forest clearing, meadow, loop, outer bounds, and north path are queried by the ordinary SM64 surface partition and floor/wall code.

This is a small static-world composition pattern, not a dynamic region manager. The registry currently has two visual modules: the expanded forest and its northbound connector. Kokiri Forest and Hyrule Field are not attached to this Castle area.

## Forest route and current footprint

Coordinates use the existing SM64 world axes and units; the western path runs toward increasingly negative X, while the forest route progresses toward positive Z.

- Existing wooded path: approximately X `-7420` through `-11100`, Z `720` through `2200`, at Y `260`.
- Clearing and meadow: Z `2200` through `8000`; terrain rises from Y `260` to roughly `440` and widens to about X `-11000` through `-8000`.
- A main dirt trail crosses the meadow. A west loop leaves it near Z `6200` and rejoins near Z `8000`.
- Low collision walls define the meadow's long outer edges. Four crossed-plane pines and an original stone marker provide simple, vertex-colored boundaries and a landmark.
- The north trail narrows and climbs from Z `8000` to an open endpoint at Z `11600`, reaching about Y `560`. A timber marker identifies the route. The open end is a future attachment point, not a connected Kokiri entrance yet.

All new geometry uses local, vertex-colored placeholder art. It adds 70 triangles and 109 render vertices across the forest and connector display lists. The new collision surfaces add 24 triangles to the existing 24-triangle connected corridor/clearing group, making that group 48 triangles. The collision stream remains far below its expanded static-surface pool in host validation. These counts are source-level; no GPU performance measurement has been made.

## Build and collision limits

The connected flag opts into the existing 64 by 64 grid with 1024-unit cells and signed-16-bit coordinates. The represented world coordinates must remain in `[-32768, 32768)`. The local test geometry is inside that range and its cell indices are checked by `ios/test_connected_world.py`.

The current expanded loader allocates 8192 static surfaces and 32768 surface nodes. This is a shared, fixed capacity, not per-region budgeting. Static collision load and broad area rendering do not yet unload or frustum-cull regions independently. Each future region therefore needs bounds and triangle-count checks before being appended; a large kingdom will eventually need chunked render submission and explicit collision/resource residency rather than one indefinitely growing Castle array.

The area still uses Castle Grounds' skybox, ambient/environment code, camera setup, object lists, and far plane. There is no region-specific lighting, weather, audio, or fog blend. Current coordinates fit the engine's existing signed representation, but world-origin rebasing is not implemented and should only be considered if measured precision or coordinate limits require it.

The north endpoint has no receiving geometry. Walking past it can leave the authored collision and fall; the north marker does not teleport or otherwise transfer Mario. Hyrule's imported level remains separately selectable/registered when its own flag is enabled; no Castle-to-Hyrule seamless path exists in this mission.

## Extension contract

For another small static region, add a render module to `registry.inc.c`, append its collision vertices and surfaces in the collision fragments, validate all global indices and shared boundary coordinates, and sample seam floor queries. Keep it under `IOS_CONNECTED_WORLD_TEST`; do not change the default Castle branch. A region seam needs identical or explicitly bridged positions and elevations in both render and collision data.

Before expanding to multiple imported regions, move from manually maintained global collision indices toward generated region metadata containing a stable region ID, world bounds/origin, render-list pointer, collision vertex/surface ranges, and environment tags. Use that metadata to support visibility and memory decisions while retaining shared Mario physics and continuous world coordinates. Dynamic streaming, region-specific environments, additional imported Nintendo assets, and device performance validation are future work.
