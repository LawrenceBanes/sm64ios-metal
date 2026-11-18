# Mission 67H.2 — Fast3D trace and WSC compatibility

The trace is an offline aid for deciding what to represent in WSC. It records
resource provenance, ranges, and command state. It omits vertex values, triangle
indices, texture pixels, and any converted scene. WSC1, WSC2, gameplay, and the
Metal presentation code use their existing paths.

## Scope and references

- Shipwright `de1ab1b63383c15f694a7c551300cccc28c63365` establishes the typed
  room commands and mesh pass roots. [Scene factory](https://github.com/HarbourMasters/Shipwright/blob/de1ab1b63383c15f694a7c551300cccc28c63365/soh/soh/resource/importer/SceneFactory.cpp)
- libultraship `da6daf1a50a5e46d6641408d52227e31e85e46df` establishes ODLT
  packet widths, tagged segments, vertex-load scale, and RDP tile state.
  [Factory](https://github.com/Kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/resource/factory/DisplayListFactory.cpp)
  · [Interpreter](https://github.com/Kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/interpreter.cpp)
- zeldaret/oot `14759abf9abb4295de142cdc1452998630ea70fb` establishes which
  state comes from the game's pass setup and runtime segment bindings.
  [Scene draw setup](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_scene_table.c#L1176-L1232)
  · [Normal room draw order](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_room.c#L77-L113)

The implementation reuses the project's existing O2R reader. The optional
reference test builds three unmodified MIT Fast3D methods from the caller's
checkout in a temporary directory and copies the corresponding license there.
No upstream source is vendored by this change.

## Support matrix

| Surface | Supported trace behavior | Limits |
| --- | --- | --- |
| Scene and room resources | Legacy binary headers and shape-0 opaque/translucent roots in room-table order. Texture V0/V1 descriptors and vertex counts from typed OVTX and OARR resources. | Alternate scene setup selection, shape-1 and shape-2 room rendering, resource sidecars, collision conversion, texel decoding, and runtime room residency are outside this tracer. |
| Calls and addresses | F3DEX2 ODLT packets, hashed texture/vertex/list references, explicitly bound tagged segments, segment aliases via MOVEWORD, direct calls and tail branches, ENDDL. | Unbound segments produce unresolved events. Hashed tail branches, dynamic conditional paths, and microcode switches are unsupported. A known returning call can leave subsequent draws tainted. |
| Vertex state | VTX and VTX_OTR_HASH byte offsets and destination intervals, TRI1/TRI2/QUAD slot checks, provenance ranges, raw geometry bits, and texture scale at VTX time. | Matrices, lighting, vertex mutation, culling decisions, and triangle visibility are unevaluated. The pinned CULLDL handler falls through and the trace labels that approximation. |
| Texture state | TEXTURE, SETTIMG, SETTIMG_OTR_HASH, SETTILE, SETTILESIZE, LOADBLOCK, LOADTILE, LOADTLUT. Eight tile descriptors, two logical input references, packed byte ranges, row strides, palette-bank provenance and partial TLUT coverage. | Exact-base load lookup, no full TMEM alias/swizzle model. Packed 4-bit subregions, replacement scaling, higher LOD bases and loaded-subregion UV normalization can be approximate. |
| Material state | SETCOMBINE selector fields, ENV/PRIM/FOG/BLEND color commands, PRIMDEPTH, geometry and high/low other-mode writes, depth and alpha flags. | The tracer records selectors and flags. It leaves two-cycle evaluation, selector cancellation, blender equations, lighting, and depth/alpha output unverified. Missing pass defaults are reported as unknown. |

Limits include 128 MiB on-disk archive size, the catalog's entry and expanded
archive limits, 4 MiB per reached resource, 64 MiB total read, 64 rooms, 4,096
commands per list, 24 nested calls, 40,000 executed commands, and 10,000 draw
records. The offset must land on a decoded packet boundary. Cycle checks use
the resource path and packet-relative offset. Failed traces retain the evidence
collected and exit nonzero.

## JSON trace v1

The root contains `format`, `version`, `scene`, `rooms`, `ordering`,
`coverage`, `issues`, and `draws`. Coverage separates decoded lists from lists
whose traced command flow completed without issues. Draw records are ordered
by room table index, then by opaque and translucent source order within each
room. This is a deterministic analysis order. Whole-scene frame scheduling is
separate.

Each draw reports scene, room, pass, display list and byte offset, draw ordinal,
primitive count, vertex resource ranges and load-time scale, texture load and
palette provenance, both tile and UV transforms, material selectors, alpha/depth
state, missing values, approximations, and WSC2 gaps. Display-list provenance
offsets count from the resource body. Caller segment offsets count from the
first packet. The status describes state available to the selected trace model,
with `null` for unknown decoded values. It does not establish rendered pixels.

Excerpt from the original test fixture after its segmented scroll command:

```json
{
  "scene": "studio/scene",
  "room": "studio/room",
  "pass": "opaque",
  "display_list": "studio/opaque",
  "order": 1,
  "command": "TRI2",
  "primitive_count": 2,
  "status": "complete",
  "wsc2_gaps": [
    "independent texture inputs",
    "sampler clamp, mirror or independent mask period",
    "tile origin requires baking or a lowered scroll state"
  ]
}
```

## WSC2 lowering decisions

| Neutral state | WSC2 fit | Next step |
| --- | --- | --- |
| Static indexed texture and fully resolved palette | An RGBA PNG plus existing batch descriptor can preserve the result. | Validate palette generation, dimension, stride and reachable texel range before reusing the PNG path. |
| Single repeat sampler and fixed render rectangle | Existing UVs and repeat sampling can represent an affine, proven bake. | Keep source size, loaded size, tile extent and origin distinct in the lowering step. |
| Clamp, mirror and independent mask periods | WSC2 fixes the sampler to repeat. | An extension needs explicit per-axis sampler/extent fields where baking cannot preserve the behavior. |
| Independent texture inputs | WSC2 supplies one texture per batch. | A future version needs two bindings and independent transforms for the required materials. |
| Combiner, primitive/environment color, alpha and depth | WSC2 supplies fixed combiner and pass modes. Tint multiplies vertex RGBA. | Represent only the measured combinations. Environment color is a distinct combiner input. Cutout, decal and primitive depth need explicit state. |
| Dynamic scroll | WSC2 supports one frame-driven repeating scroll slot. | Use an existing slot for a proven one-tile equivalent. Multiple tiles and pass-specific initialized state require separately validated descriptors. |

Keep WSC1/WSC2 readers compatible. If fixtures show a need for a format extension,
define a new version with explicit material capabilities and test coexistence
before adding any runtime reader changes.

## Real archive evidence

Two runs on the local NTSC 1.0 archive (`EC7011B7`) produced byte-identical
JSON in the ignored `build/mission67h2` directory. The default scene has three
room headers and six mesh roots. The trace decoded 40 unique display lists,
with 33 completing without unresolved issues in their traced call flow. It
encountered 2,947 commands and recorded 1,054 candidate draws, of which 1,009
were opaque and 45 translucent. The record counts include 2,060 candidate
primitives. All 1,054 draws are incomplete because the pass prelude state is
absent. The branch does not register a playable scene.

The six unresolved calls reference 0x08, 0x09, 0x0a, 0x0b twice, and 0x0c.
The pinned scene handler creates these segments during rendering. In addition,
472 draw records lack established TLUT interpretation for tile 0. Potential
combiner references identify 448 draws with a second texture input, 425 with
environment color, 642 with primitive color or constants, 386 with sampler
constraints, and 25 with alpha or depth features beyond the straightforward
WSC2 pass. These counts overlap and include syntactic mux references while
cycle selection is unknown. The next technical milestone is a format-neutral
state prelude and lowering harness for original or authorized input, followed
by SM64-managed room handoff tests.

```sh
python3 tools/o2r_draw_trace.py ~/Downloads/oot.o2r \
  scenes/shared/spot04_scene/spot04_scene --output build/mission67h2/kokiri-trace.json
```

## Validation

- Nine authored trace tests cover typed resolution, hashes, vertex windows,
  two tiles, TLUT coverage, load-time UV scale, selected other-mode writes,
  scroll and environment updates, pass order, truncated packets, cycles and
  budgets. The Python trace tests also passed with development mode and the
  debug allocator enabled.
- `tests/check_fast3d_trace_reference.py` compiled `GfxDpSetTile`,
  `GfxDpSetTileSize`, and `GfxDpLoadTlut` from the pinned source and matched
  descriptor fields, tile bounds, and the CI4 overwrite of a CI8 palette under
  AddressSanitizer and UndefinedBehaviorSanitizer. The remaining fixture checks
  use expected field values independently checked against the pinned source.
  This is scoped reference parity. Full draw execution and pixel parity remain
  untested. The Python interpreter itself is covered by Python tests.
- The Linux regression suite and existing WSC1/WSC2 package replay passed.
  The native package replay also passed under ASan/UBSan. Local sanitizer
  checks ran with LeakSanitizer disabled.
- The final Linux suite reported 90 Python cases and passed its native and
  build checks. The actual archive trace took 2.22 seconds and peaked at
  90.1 MiB RSS in one local run.
- [Apple validation run 44421679105](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44421679105)
  passed both jobs for implementation commit
  `0e66d38849f97460db9ff14712f05a4e55201f37`. Xcode 27.1 (27A9275) and the
  iOS SDK compiled and linked the iPhoneOS and simulator validation targets
  and produced 12 Metal libraries. The asset-free validation artifact was
  downloaded under `build/mission67h2/apple`. The first workflow was
  superseded while its Linux package install was active. The dedicated
  workflow ran the Apple job independently.
- This milestone uses the host trace and asset-free validation targets.
  The latest full-device IPA remains Mission 67G.
