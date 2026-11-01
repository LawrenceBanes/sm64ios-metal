# Hyrule Field importer: first implementation boundary

## First real archive pass

The supplied archive is available locally at `/home/larry/Downloads/oot.o2r` (32 MiB; SHA-256 `e3bf3ae3565768998026e60113ed3fc506fc5415e23299de41b9b42a5fbc3807`). It passes ZIP CRC validation and contains 38,390 file entries. The selected `scenes/shared/spot00_scene` prefix contains 591 entries: 445 display lists, 52 textures, 38 arrays, 24 Scene resources, 15 cutscenes, 5 paths, 1 collision header, and 11 headerless/short files.

The canonical Hyrule Field root is `scenes/shared/spot00_scene/spot00_scene`; the folder also contains room and alternate-header Scene resources. Root selection now prefers that canonical path rather than an arbitrary sorted entry. The importer fully parsed all 13 commands in the root and all 24 Scene resources (including room and alternate-header metadata); its scene graph has 74 referenced resources, all present in the archive. The `SetCutscenes` command's payload is a length-prefixed resource path, consistent with Shipwright's [SetCutscenesFactory](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/soh/resource/importer/scenecommand/SetCutscenesFactory.cpp).

The root's CollisionHeader v0 was decoded to 1,162 vertices and 1,579 collision triangles. All 18 root start positions lie over an upward-facing collision triangle within the importer check's vertical range. All collision triangles are nondegenerate, and all 1,579 computed triangle normals agree with the archived collision normals; the declared collision bounds contain the vertices. The output is a collision debug OBJ plus metadata, not the textured visual scene. Conversion output remains in `/tmp/hyrule-field-import`; no archive-derived data was added to Git.

The render blocker is now concrete: the scene references 445 ODLT display-list resources, 52 OTEX textures, and 38 Array resources under the scene prefix. The importer indexes but does not interpret their RDP/GBI command streams, vertex payload indirections, texture encodings/palettes, or material state. Other object banks are separate resource dependencies. Until those are decoded into runtime geometry/materials, the field is not renderable or playable in SM64. Coordinate units remain source OoT units at an explicitly provisional 1:1 scale; collision and spawn geometry agree internally, but scale still needs comparison in the target engine.

## Resource format findings

Shipwright's O2R archive wrapper treats `.o2r` as a ZIP and indexes each non-directory member by resource path. The resource loader reads a 64-byte OTR resource header: byte-order marker, custom flag, two reserved bytes, four-byte resource type, resource version, 64-bit resource identity, and reserved remainder. The type identifies the parser factory; it is not a geometry file extension. Scene (`OROM`), collision (`OCOL`), vertex (`OVTX`), display-list (`ODLT`), and texture (`OTEX`) are separate resources. Binary scene bodies begin with a command count and contain variable-sized commands; geometry is referenced by resource paths. See the upstream [O2R archive reader](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/ship/resource/archive/O2rArchive.cpp), [resource loader](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/ship/resource/ResourceLoader.cpp), [scene factory](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/soh/resource/importer/SceneFactory.cpp), and [collision factory](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/soh/resource/importer/CollisionHeaderFactory.cpp).

The scene importer records starts, actors, collision header paths, room lists, entrances/exits, objects, lights, mesh display-list references, and supported environment settings. It stops at the first command whose payload is not implemented, and records the exact command and offset. Dependency paths are resolved against the ZIP index, and missing references are listed explicitly. CollisionHeader version 0 is decoded into vertices, triangles, normals, surface type words, camera data, and water boxes. Collision-derived region bounds and an OBJ are emitted as a **debug geometry export**; neither is a textured/renderable Hyrule scene or loaded by the game yet.

Planned scene IDs follow the OoT decompilation's [scene-name mapping](https://github.com/zeldaret/oot/blob/main/tools/namefixer.py): Hyrule Field is `spot00_scene`, Kakariko is `spot01_scene`, Zora's River is `spot03_scene`, Kokiri Forest is `spot04_scene`, Lake Hylia is `spot06_scene`, Gerudo Valley is `spot09_scene`, Lost Woods is `spot10_scene`, Hyrule Castle surroundings are `spot15_scene`, Death Mountain Trail is `spot16_scene`, and Lon Lon Ranch is `spot20_scene`.

O2R resources also include references outside the scene prefix (object banks, textures, and display lists). They must be resolved and their formats decoded before this tool can make a complete region package. N64 display-list execution carries material state and vertex references; dumping those command bytes as mesh geometry would be incorrect. Texture dimensions and raw pixels do not by themselves reveal the N64 format, palette, or material interpretation.

## Run

```sh
python3 tools/hyrule_importer.py /path/to/oot.o2r /tmp/hyrule-field
```

The tool verifies ZIP CRCs, emits a sorted resource index and versioned `hyrule-world.json`, parses supported scene/collision records, and writes collision metadata/OBJ to the chosen output directory. `--scene-prefix` selects another scene path; `--scale` sets a provisional positive coordinate scale. The current coordinate mapping preserves X/Y/Z (Y-up) with a configurable unit scale. Scale is explicitly marked provisional until compared against in-game dimensions.

The test archive is generated in memory from format fixtures and contains no game data. Fourteen host tests validate archive/header/scene/collision parsing, deterministic indexing, present and missing dependency reporting, coordinate scaling, root-scene selection, collision floor queries and winding/bounds checks, region mapping, path safety, and failure reporting. The first real-archive figures above were produced by the same CLI and are kept out of the test fixtures.

## Hyrule region and streaming state

[`hyrule_regions.json`](hyrule_regions.json) reserves region IDs and records the intended Hyrule Field → Lon Lon Ranch connection as pending coordinate calibration. Other edges are unmapped; they are not assumed to correspond to physical adjacency. This is data planning only. There is no runtime region loader, Metal resource activation, bounded residency manager, or cross-region collision continuity yet. Those need a validated scene package and must keep the currently occupied region pinned.

## iOS/Metal integration limit

Nothing here changes the SM64 renderer or level loader. The existing runtime currently consumes compiled SM64 levels and N64-style `Gfx`; the importer’s intermediate format is not yet a runtime level format. A real playable Hyrule Field requires display-list/texture/material conversion, collision adaptation, a scene selector, spawn selection, region ownership, and device verification. Until `oot.o2r` is parsed and those paths exist, **there is no playable Hyrule Field and no seamless streaming claim**.
