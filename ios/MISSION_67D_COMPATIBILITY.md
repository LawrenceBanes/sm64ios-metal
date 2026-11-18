# Mission 67D scene compatibility and entrance mapping

This is an offline metadata audit of the 67B inventory and the supplied
archive. The package runtime and gameplay remain at the 67B implementation.
The validator ships without game assets or mapping tables.

## Catalog findings

| Measure | Verified count or interpretation |
| --- | --- |
| Archive | 38,390 entries, 101 scene roots (89 shared and 12 non-MQ), 388 room headers, 385 alternate headers |
| Default entrances | 442 local slots across 101 complete default headers, all with in-range player entry and room references |
| Room transition actor records | 638 across all audited headers. 426 connect different rooms, 209 reference the same room on both sides, and three specify no room on one side. |
| Exit list records | 1,118 across 218 headers and 100 roots. 372 are from default headers and 746 from alternates. They contain 264 distinct destination indices. |
| Special cases | Twelve records use five indices above the last reference table row, `0x613`. Contextual returns require separate handling. |

The 67B catalog's 635 resolved room records include the 209 same-room
records. The other three use byte `0xff` in a signed room field. Upstream room
setup treats a negative side as absent. The totals count entries by header,
including repeated variants. They do not count unique doors or prove
traversal. [Room setup source](https://github.com/zeldaret/oot/blob/main/src/code/z_room.c#L587-L622)

The ZIP's `version` record contains `EC7011B7`, which the exporter identifies
as NTSC 1.0. This establishes the declared revision family. No catalog
resource name or recognized type identifies a global entrance table. The
scene-local exit values cannot recover that table.
[OTRExporter revisions](https://github.com/HarbourMasters/OTRExporter/blob/develop/rom_info.py)

## Entrance lookup contract

1. The source header's exit-list slot contains a base entrance index. For
   a floor-triggered exit, the collision polygon selects a surface type.
   A nonzero exit field in that surface selects an exit slot by subtracting
   one. A zero **exit field** means no floor exit. A zero **base index** in
   the exit list is an ordinary table index. Other triggers, including
   actors, need their own validation.
   [Surface field](https://github.com/zeldaret/oot/blob/main/src/code/z_bgcheck.c#L3788-L3790),
   [Player exit handling](https://github.com/zeldaret/oot/blob/main/src/overlays/actors/ovl_player_actor/z_player.c#L4842-L4873)
2. For ordinary entrances, the original game reads the global entrance
   table at base index plus the selected scene layer. That row supplies
   the destination scene table ID and **local entrance slot**. Age, time,
   quest state and cutscene context can affect layer selection. A nonzero
   layer can change both the table row and the active header. Contextual
   returns can use saved state or separate return tables.
   [Entrance table](https://github.com/zeldaret/oot/blob/main/include/tables/entrance_table.h),
   [Game layer selection](https://github.com/zeldaret/oot/blob/main/src/code/z_play.c),
   [Alternate header dispatch](https://github.com/zeldaret/oot/blob/main/src/code/z_scene.c#L462-L488)
3. The scene table identifies the scene resource and draw setup. The active
   header's spawn list resolves the local slot into a room index and
   player entry index. The player-entry list supplies position, rotation,
   and parameters. Respawn can override the room. Scene ID, local slot,
   player entry, and room ordinal are separate fields.
   [Scene table](https://github.com/zeldaret/oot/blob/main/include/tables/scene_table.h),
   [Spawn type](https://github.com/zeldaret/oot/blob/main/include/scene.h#L25-L49),
   [Spawn resolution](https://github.com/zeldaret/oot/blob/main/src/code/z_scene.c#L248-L284)

### Kokiri Forest default header

Kokiri is scene `0x55` in the reference scene table. Its default header
lists three rooms and 12 local entrance slots. Slot 1 selects room 1 and
the other slots select room 0. Two default transition actors connect room 0
with rooms 1 and 2. The scene's 29 cataloged room records include alternates.
The scene and room headers each have ordered alternate arrays with empty
slots. Preserve those positions and select one setup at a time.

A local comparison against ten explicit layer-zero rows from the reference
tables produced valid destination metadata for all 12 default exit slots.
For example, slot 2 cites `0x5e0`, which resolves to Lost Woods scene
`0x5b`, local slot 9. The archive's destination header selects player entry
9 in room 5. Slot 1 cites `0x000`, which resolves to scene `0x00` (Deku Tree),
local slot 0. Two polygons use exit field 2 and select this slot. The
second zero at slot 11 has no referencing polygon in this static collision
header. [Reference entrance rows](https://github.com/zeldaret/oot/blob/main/include/tables/entrance_table.h)

Nine slots have references from 16 static collision polygons. This shows
the archive-to-metadata join for the default scene. It leaves trigger
conditions, door actor behavior, safe arrival, and return routes unverified.
The same ten rows matched 250 raw records across the catalog with destination
layer forced to zero. Another 868 records had no mapping supplied. These
are comparison results using the upstream reference table. Before enabling
gameplay, verify the complete table and selection policy against the exact
source revision.

## Minimum compatibility work

| Resource | Verified default Kokiri dependency | Required work |
| --- | --- | --- |
| Scene and room headers | Version-0 resources carry a command count and typed payloads. Commands 4, 6, 0, 10, 14, 19 and 24 carry room order, local spawn slots, player entries, mesh shape, transition actors, exits and alternates. All three rooms use shape type 0 with both opaque and translucent lists. | Select one setup and resolve its ordered scene and room headers. Validate each mandatory command and dependency. |
| Display lists and vertices | Six root lists reach 40 display lists and 34 vertex arrays. ODLT supplies a microcode tag, aligned command words and wide hashed resource references. | Validate list bounds, recursion and vertex windows. Check the material combiner, alpha, depth, winding and render order using captures. |
| Runtime segment calls | Six calls in four reachable lists target segments `0x08` through `0x0c`. The upstream scene draw routine fills those slots. Slots `0x08`, `0x09`, `0x0c` carry texture scroll commands. Slots `0x0a` and `0x0b` update environment color. | Model these as runtime state dependencies for each pass and setup. A larger ZIP search cannot supply those targets. Any static approximation needs a reference comparison. |
| Textures and palettes | 60 reachable texture records. 33 RGBA16, 17 indexed 8-bit, five intensity 4-bit, three intensity-alpha 4-bit and two intensity-alpha 8-bit. | Resolve palette and tile bindings per draw. Check decoded dimensions, UVs, wrapping, mirroring, coordinate shifts and opacity. |
| Static collision | One scene header with 1,081 vertices, 1,692 polygons, 46 surface types, 15 camera entries and one water box. The vertex and polygon numbers are collision counts. The rooms do not each carry a static mesh. | Keep scene collision resident or derive explicit room ownership and validate seams. Apply the same transform to geometry, collision and spawns. Verify floor, ceiling and wall clearance and define water and surface policies. |
| Actors and environment | Room actor lists contain 78, nine and 12 entries. Object lists contain 11, five and four indices. Scene lighting and environment settings are present. | Identify dependencies that affect traversal, including doors, loading planes and moving collision. Evaluate camera behavior separately from rendering static terrain. |

The scene factory dispatches versioned command loaders. The collision
factory reads scene-wide polygons followed by separate surface, camera and
water data. The upstream draw configuration confirms the segment state
dependencies. [Scene factory](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/soh/resource/importer/SceneFactory.cpp),
[Collision factory](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/soh/resource/importer/CollisionHeaderFactory.cpp),
[Kokiri draw setup](https://github.com/zeldaret/oot/blob/main/src/code/z_scene_table.c#L1078-L1127)

## Reproduce the audit

```sh
mkdir -p build/mission67d
python3 tools/audit_o2r_entrances.py \
  ../sm64ios-mission67/build/mission67b/o2r-inventory.json \
  --output build/mission67d/exit-audit.json
```

The neutral validator accepts ordered room keys and separate local player
and room indices. The inventory adapter reports `metadata_match`,
`missing_mapping`, `invalid_destination`, or `special_context_required`.
Add `--mapping <local-map.json> --layer <offset>` to validate explicit rows.
The version-1 `scene-entrance-map` document requires `source` (provenance and
revision), `catalog_sha256` (SHA-256 of the exact input JSON bytes) and
`rows`. Each row contains integer `base`, `layer`, `table_row`,
`scene_id`, `local_slot`, plus destination root key `scene` and selected
header key `header`. Optional `special` lists contextual base indices.
Each row must satisfy `table_row = base + layer`. A selected alternate must
belong to the destination root and supply complete room, spawn and
player-entry lists. There is no implicit layer fallback or default header
inheritance. The catalog digest checks input compatibility. It does not
prove the mapping source is authoritative. Invalid or partial rows remain
in the report. Missing maps are expected for inventory-only analysis.

## WSC1 boundary and priorities for a future authorized pipeline

WSC1 supports authored packages, one optional opaque PNG per room, and
room and scene transitions tested through its host provider. Live gameplay
attaches the default room to stock terrain, uses the stock Mario spawn,
and tears down with the area. WSC1 lacks O2R conversion, multiple material
batches, separate translucent passes, live room changes and a
gameplay spawn/return adapter.

1. **Pin one supported setup.** Store the selected source revision,
   scene ID, header key, and local slot separately from the SM64 package ID.
   Match the entrance table and selection policy to the archive. Keep
   zero-based source room ordinals separate from nonzero WSC1 room IDs.
2. **Validate materials first.** On authorized scene inputs, extend the
   package contract for material batches and translucent draws. Compare
   representative opaque, indexed and translucent submissions against
   a reference. Keep the constant-alpha regression. Earlier invisible
   Hyrule terrain still needs separate capture and device confirmation.
3. **Validate arrival and departure.** Keep scene-wide collision resident
   initially. Verify floors, walls and clearance at every selected spawn.
   Add separately validated room boundary triggers and a stock-area return.
   Clear or reacquire cached surface references before freeing live rooms.
4. **Verify one round trip on device.** Test registration and rollback on
   original fixtures. Validate the Apple build, all three rooms, camera,
   transparency, input and memory on hardware before calling any route
   playable.
