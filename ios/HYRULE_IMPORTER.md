# Hyrule Field importer: first implementation boundary

## Current blocker

No `oot.o2r` file was found under `/home/larry`, `/tmp`, `/mnt/data`, `/mnt`, `/media`, or Downloads. The importer takes the archive path directly, so it can be run as soon as the file is attached or its local path is supplied. No ROM-derived data has been added to this repository.

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

The test archive is generated in memory from format fixtures and contains no game data. Eleven host tests validate archive/header/scene/collision parsing, deterministic indexing, present and missing dependency reporting, coordinate scaling, region mapping, path safety, and failure reporting, not the actual `oot.o2r` archive. On a real archive the tool prints actual decoded counts; no archive counts are available yet.

## Hyrule region and streaming state

[`hyrule_regions.json`](hyrule_regions.json) reserves region IDs and records the intended Hyrule Field → Lon Lon Ranch connection as pending coordinate calibration. Other edges are unmapped; they are not assumed to correspond to physical adjacency. This is data planning only. There is no runtime region loader, Metal resource activation, bounded residency manager, or cross-region collision continuity yet. Those need a validated scene package and must keep the currently occupied region pinned.

## iOS/Metal integration limit

Nothing here changes the SM64 renderer or level loader. The existing runtime currently consumes compiled SM64 levels and N64-style `Gfx`; the importer’s intermediate format is not yet a runtime level format. A real playable Hyrule Field requires display-list/texture/material conversion, collision adaptation, a scene selector, spawn selection, region ownership, and device verification. Until `oot.o2r` is parsed and those paths exist, **there is no playable Hyrule Field and no seamless streaming claim**.
