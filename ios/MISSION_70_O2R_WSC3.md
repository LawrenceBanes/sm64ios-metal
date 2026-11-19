# Mission 70 — Local O2R scene import

The host converter reads a ZIP-based O2R archive and stages a WSC3 package and its external PNGs. The iOS developer build reads the staged files from Documents. Run the converter on a computer with Python 3. The phone reads WSC3 directly.

## Convert and load

Use an archive containing a scene you are authorized to adapt. Quote the exact scene resource name from the archive inventory.

```bash
python3 tools/o2r_inventory.py /path/to/authorized.o2r --output /tmp/o2r-inventory.json
python3 tools/o2r_to_wsc3.py /path/to/authorized.o2r myproject/scenes/atrium /tmp/atrium-install
```

The converter requires a new output directory. It writes `scenes/4100.wsc`, referenced PNGs under `gfx/scenes/`, and a local `conversion-report.json`. Copy the first two trees into the app's Documents directory using the active app/container's Files or file sharing interface. Keep the report on the host. Install one package at a time in slot 4100. Resume active gameplay in Castle Grounds, open **Gameplay → Load Local WSC3 Scene**, and use **Leave Local WSC3 Scene** to return. Replacing the package while outside the imported area is supported. The original WSC2 and WSC3 diagnostic entries continue to use their bundled packages.

Use the existing app build with `IOS_O2R_SCENE_RUNTIME=1 IOS_SCENE_DIAGNOSTIC=1 EXTERNAL_DATA=1`. The stock collision grid is the converter default. `--expanded-grid` requires a build with `IOS_CONNECTED_WORLD_TEST=1` or `IOS_HYRULE_WORLD=1`. Use `--scale 1/2 --offset X Y Z` to fit room positions and collision into legacy engine bounds. A positive uniform scale and translation apply to position data for rendering, collision and spawn. Coordinates must fit signed 16-bit components and the selected X/Z collision grid. Conversion fails for degenerate collision and nonmatching source winding.

The WSC3 material and package schema is documented in [Mission 67H.4](MISSION_67H4_WSC3.md). The iOS package slot is dedicated to Castle Grounds area 4. Entry 0 is selected. The native area loader validates texture files, registers all rooms, checks the spawn against SM64 floor, wall and ceiling queries, and only then gives it to the stock warp spawn path. A failed load returns to Castle Grounds. Up to 16 rooms are resident together within the region collision budget. Room changes track the player's position while keeping neighboring and distant collision present throughout the visit. Leaving the area removes room geometry, collision, and renderer texture references. Placing contiguous room geometry and floors at the same world-space seam remains the content author's responsibility.

For the current dry-run audit, bounded subtexture loads, private game builds, and device test procedure, see [Mission 71](MISSION_71_OPEN_WORLDS.md).

## Optional layout

`--layout layout.json` supplies scene authoring decisions that cannot safely be inferred from the archive.

| Key | Meaning |
| --- | --- |
| `collision_rooms` | For one shared collision header across multiple rooms, supply one zero-based room-table index per source polygon. Distinct room-local collision headers need no mapping. |
| `bounds` | One `{"min":[x,y,z],"max":[x,y,z]}` record per room in output coordinates. Omit to derive bounds from rendered and collision geometry with vertical margin. |
| `links` | Additional pairs of zero-based room-table indices. The converter also reads valid front/back room indices from source transition records. |
| `surface_map` | Explicit source surface index to SM64 surface ID mapping. For an unmapped source, only all-zero surface metadata maps to the stock default surface. |
| `entrance` | Source entrance index to use in native slot 0. Defaults to source index 0. Output reports the choice. |
| `pass_preludes` | Optional `opaque` and `translucent` keys naming state-only ODLTs in the archive. Use these when room display lists inherit required state. |

Use `--segment 08=path/to/list` for explicit static segmented references. Multiple segment options are accepted. Source groups, room indices, command order and files are resolved from headers and typed resources.

## Supported conversion

| Area | Supported |
| --- | --- |
| Archive | Bounded ZIP O2R resources with binary endianness markers, v0 scene, vertex, collision and display-list headers, and v0/v1 texture headers. |
| Geometry | Normal room meshes, F3DEX2 triangles and quads, typed vertex arrays, bounded segmented or CRC64-hashed references, precolored vertices, and static source UVs. Room sources retain order within each render pass. |
| Textures | Whole-resource, linear `LOADBLOCK` payloads in RGBA16/32, IA4/8/16, I4/8, and CI4/8 with explicit RGBA5551 or IA88 palette state. The last physical TLUT load owns each index. PNG identities are content-addressed. |
| UV and sampling | Vertex-load UV scales, independent WSC3 tile shifts and base origins, point/three-point filtering, and whole-tile clamp, repeat or mirror when the tile period matches the image. |
| Materials | Untextured color or shade, single texture and supported color modulation, dual texture interpolation using environment alpha, opaque depth state, and the straight-alpha translucent preset. The native WSC3 shader evaluates the translated WSC equation. |
| Collision | Static floor, walls and ceilings from bounded source polygons. The converter checks indices, surface mappings and normals. Each entrance must have a walkable floor, edge clearance and headroom. The runtime also checks the default entrance using the real collision queries after all rooms register. |

Missing or unsupported draw commands reject the export. The CLI reports resource paths, pass, command offsets and reasons on standard error. No partial package is installed. Add matching source or prelude state, bind the required static segment, or prepare a supported source material and rerun the export. Scene metadata without a runtime equivalent appears under `unmapped_metadata` in the conversion report.

Unsupported features include image or cull-sphere mesh roots, matrix-driven mesh transforms, texture subranges and aliases, dynamic command generation, tile-specific mask periods, automatic scroll and environment alpha animation extraction, fog, calculated lighting and texgen, custom blend or coverage modes, cutout threshold translation, nonpreset depth modes, water boxes, actor/door spawning, and cross-scene exit routing. Room transitions are continuous movement through authored geometry. The exporter does not reconstruct game-specific event or entrance-table policies. SM64 gameplay and presentation remain authoritative.

## Build boundary

The Apple asset-free workflow validates SDL2 and Metal/MSL. It does not generate a game IPA. The full game target requires extracted stock SM64 inputs during compilation, including inputs needed for generated code and the runtime resource pack. An IPA without those assets requires a separate boot path or a complete authorized replacement asset set. No O2R archive or converted user scene is needed for asset-free CI, and neither is staged by this mission.
