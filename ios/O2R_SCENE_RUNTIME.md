# O2R scene pipeline foundation

## Current host-side pipeline

`tools/hyrule_importer.py` contains the existing O2R/OTR binary readers and a
single-scene lowering path. `tools/o2r_inventory.py` adds a format-neutral,
read-only catalog pass for supported scene, vertex-array, collision-header,
display-list, and texture records. It follows scene-room and hashed-resource
dependencies, reports unsupported or unresolved records, validates archive
paths and limits, and can write a deterministic version-1 JSON catalog with:

```sh
python3 tools/o2r_inventory.py /path/to/archive.o2r --verify-crc \
  --output build/o2r-scene-catalog.json
```

The catalog is metadata and parser coverage only. It does not export geometry,
textures, executable assets, or a playable scene. The new tests use synthetic
ZIP/O2R records only; the supplied Nintendo archive was not processed for this
mission.

## Runtime scene package contract

`src/engine/world_scene.h` defines an asset-provider boundary for future
versioned packages. A provider loads a scene package containing stable scene,
room, entrance, and exit identifiers. Room callbacks are responsible for
registering that room's render and collision resources with the existing SM64
systems. The manager provides:

- Package and room-graph validation before activation.
- Destination validation and transactional scene replacement: the destination
  room activates before the source scene is released; a failed load or
  activation leaves the source active.
- Same-scene room changes and declared exit traversal.
- Near-room prefetch based on player bounds, configured displacement bound,
  and distance; the containing room and its declared neighbors stay resident.
- A close path that deactivates all rooms and releases provider-owned data.

The manager never writes Mario or camera state. It returns a destination spawn
so the game integration can preserve health, character, mount/vehicle state,
and other session state explicitly. Provider callbacks run on the main thread.
They must be transactional on activation failure and defer GPU destruction if
the renderer still references resources.

`world_region_unregister()` releases one region's owned collision storage and
partition nodes. A provider may use this during room deactivation. It must
coordinate render-list/resource ownership separately; that is not implemented
by the region collision manager.

## Required next implementation

The current generic scene manager is a tested engine service, not yet wired to
the main gameplay loop. There is no O2R package writer/reader that converts the
full scene dependency graph into runtime resources, no provider adapter for
Metal/Gfx and collision, and no tree-triggered scene entry. The versioned JSON
catalog must evolve into a versioned, bounded package format with explicit
geometry, materials, textures, collision, room bounds, entrances/exits, and
dependency hashes. A provider then needs to:

1. Decode one package incrementally and validate every range before allocation.
2. Convert scene coordinates once and apply the same transform to render and
   collision data.
3. Register each room as an owned world region and submit its Gfx resources.
4. Keep the old scene loaded until the new entrance's room is renderable and
   collision-ready, then swap while preserving the active game session.
5. Cache shared texture/material resources with reference counts and enforce a
   measured mobile memory budget.
6. Add device validation for visibility, scale, material state, collision,
   camera, and memory before claiming any region is playable.

## Engine limits that remain

Legacy Mario and camera coordinates are area-local floats, while collision
vertices are signed 16-bit. `world_region` stores large-world metadata with
64-bit coordinates but checks conversion into the active legacy window. A
world crossing that exceeds that window still needs a coordinated rebase of
player, camera, objects, vehicles, collision, and renderer transforms.
The current room manager also does not change SM64's `Area` object lists,
environment settings, or level scripts. Those need a deliberate adapter; the
new manager does not silently bypass them.
