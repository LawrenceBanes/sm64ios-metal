# Kokiri Forest (`spot04`) compatibility audit

## Scope and source

This is a host-side inspection of Larry's local
`/home/larry/Downloads/oot.o2r`. The archive remains outside the repository.
No Zelda assets or third-party source were copied into Git. The canonical
scene is `scenes/shared/spot04_scene/spot04_scene`.

The archive index contains 38,390 entries. Under `spot04_scene`, the importer
indexes 261 resources: 34 Array, 1 CollisionHeader, 11 Cutscene, 114
DisplayList, 10 Path, 52 Scene, 60 Texture, and 21 headerless/short records.
It fully decodes 201 of those resources. The root scene contains three rooms,
12 entrance/start-position records, two transition-actor records, one
collision-header reference, three path-list references, and 12 non-default
alternate-header resources plus the default setup. Its 52 Scene resources
include room and alternate setup resources; they should not all be treated as
simultaneously visible rooms.

The referenced collision resource
`spot04_sceneCollisionHeader_008918` parses to 1,081 vertices and 1,692
triangles. The current geometry validator reports all declared bounds contain
the vertices, with no degenerate, reversed-winding, or normal-mismatch
triangles. This establishes that a real collision subset is decodable; it
does not establish that all room and transition collision is represented in
the active SM64 level.

## Importer feasibility result

A local conversion trial over the canonical root and its three declared rooms
decoded 34 vertex arrays, 60 texture resources, and 2,060 render triangles,
and emitted 40 Gfx list definitions. It reported six unresolved ordinary
segmented display-list pointers (`0x08`–`0x0c` segment references in room
lists). The current native emitter reports only one opaque and one translucent
room call for this scene description, so these counts are **format and parser
feasibility data**, not proof that Kokiri's visible geometry is complete or
correct. Kokiri has not been registered, built into the IPA, or rendered on
device.

The room/scene resources reuse the same O2R resource header and many of the
same F3DEX2 wide hash references handled by the Hyrule importer. Existing
parsers can read the root headers, rooms, collision header, texture
descriptors, vertex arrays, and many display lists. The current runtime
lowering is still a single SM64 area/display-list set and does not yet
faithfully select alternate headers, assemble all room meshes, or resolve
every segmented sub-list. Texture conversion can be reused only after its
palette/material dependencies are verified for each Kokiri setup.

## Work needed before an opt-in Kokiri level

1. **Choose a scene setup.** Decode the 12 alternate headers, identify their
   actor/object, room, lighting, and mesh changes, and select one explicit
   default (daytime) setup. Keep other setups out until their differences are
   understood.
2. **Resolve the full render dependency graph.** Walk all three rooms and
   selected setup references, map ordinary segmented pointers to O2R resource
   payloads or room-local segment bases, and reject unresolved pointers rather
   than dereferencing guessed addresses. Verify room mesh types and transforms
   before emitting native Gfx. The six known unresolved calls need exact
   referents.
3. **Validate texture/material closure.** Associate every sampled texture
   with its palette/TLUT, tile setup, combiner, and render layer. Compare
   converted pixels and representative in-game UVs; do not reuse a texture
   merely because a hash or descriptor exists.
4. **Lower scene collision through `TERRAIN`.** Convert the collision header,
   water boxes, transition actors, entrances, exits, and surface types into the
   stock SM64 level data. Preserve one consistent coordinate transform for
   geometry, collision, and spawn positions. Verify transition exits with
   collision queries and the normal `MARIO`/`lvl_init_or_update` path.
5. **Add a source fixture and opt-in build.** Extend importer tests with
   representative `spot04` resources that cover all rooms, setup selection,
   segment resolution, texture/palette closure, and collision lowering. Keep
   the level behind an explicit build flag so ordinary level/save tables do
   not change. Build an unsigned iOS IPA from the private O2R draft-asset
   workflow and validate actual render/collision behavior on device.

This plan deliberately stops before region adjacency or streaming. The first
Kokiri milestone should be a complete, selectable single SM64 level. It can
then become the next leg of the forest route after Hyrule Field is validated
in the iOS renderer.

## Private iOS build input

The iOS IPA workflow now accepts optional `hyrule_o2r_release_tag`. To request
an opt-in Hyrule-enabled build, the private repository needs a **draft** release
tag beginning `hyrule-o2r-`, with exact description:

`Temporary private Ocarina archive for Hyrule-enabled iOS IPA build.`

Attach only `oot.o2r` and `oot.o2r.sha256` (one standard `sha256sum` line).
The workflow validates the draft metadata, checksum, ZIP integrity, and the
Hyrule `spot00` root, uses the archive from the runner's temporary directory,
and deletes it and importer-generated intermediates in cleanup. The archive
is not committed or attached to the IPA or Actions artifact. The pre-existing
private SM64 extracted-asset input remains separately required.

## Mission 55.3 revalidation

The local default `spot04` import was rerun on 2026-11-15 using the shared
importer, now with Kokiri-specific native symbol names. It produced 40 Gfx
lists, 34 vertex arrays, 60 textures, 2,060 sampled render triangles, and a
1,081-vertex/1,692-triangle collision mesh. The three declared root rooms each
contribute an opaque and translucent display-list call (3 of each). The older
1-call summary was an accounting bug caused by using polygon index alone as a
dictionary key; it did not count calls from earlier rooms. The emitter now
keys variants by room and polygon so translated mesh instances cannot reuse
another room's vertex transform. The current three Kokiri default meshes are
type 0 and have no polygon translation, so this guard is a general correctness
fix rather than evidence of a previously visible Kokiri displacement.

Six segmented calls are still omitted and their dependencies are not resolved:

| Source display list | Byte offset | Segmented target |
| --- | ---: | --- |
| `spot04_room_0DL_0054B0` | 272 | `0x0b000001` |
| `spot04_room_0DL_017AC0` | 232 | `0x08000001` |
| `spot04_room_0DL_017AC0` | 280 | `0x09000001` |
| `spot04_room_1DL_0041F8` | 272 | `0x0b000001` |
| `spot04_room_1DL_0041F8` | 280 | `0x0c000001` |
| `spot04_room_1DL_0018B0` | 272 | `0x0a000001` |

Both generated Kokiri C and the default Hyrule level C compile with the Linux
host compiler. Kokiri remains an intermediate package only: no level
registration, combined collision, world transform, gameplay route, iOS build,
or device rendering is implemented or validated.

The current connected-world design proposal is documented in
`CONNECTED_WORLD_ARCHITECTURE.md`. It recommends keeping the existing SM64
loop active in one area and composing static region geometry/collision there;
it does not claim that Kokiri is already playable or adjacent to the existing
Castle connector or Hyrule import.
