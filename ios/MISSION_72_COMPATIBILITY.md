# Mission 72 — O2R/Fast3D compatibility baseline

Branch: `mission-72-fast3d-compat` (isolated from the Mission 71 and CoopDX branches)

## Implemented in this milestone

| Source feature | Status | Evidence / boundary |
| --- | --- | --- |
| Bounded F3DEX2 display-list calls and vertex windows | Supported subset | Existing tracer command, byte, recursion, cycle, room, and draw budgets remain enforced. |
| Static segmented references | Supported with explicit binding | `--segment HEX[@OFFSET]=RESOURCE` and per-room `room_segments` provide caller-supplied aliases. No guessed segment bases. |
| Static affine model-view matrices | Newly supported, with an explicit assumption | `MTX_OTR` hashed matrices and segmented `G_MTX` resolve O2R `Matrix` resources encoded as 64-byte N64 fixed 16.16 matrices. Load/multiply, push, and pop are replayed at vertex-load time, matching the RSP vertex-cache timing; the trace records the matrix captured by each draw and transforms are baked into world coordinates. The importer cannot infer whether an affine model-view matrix contains a runtime camera transform, so users must only provide deterministic model/world transforms. |
| Fog source state | Supported in WSC4 subset | Signed `MOVEWORD G_MW_FOG` coefficients and `SETFOGCOLOR` are traced with provenance. The exporter stores the fog parameters in WSC4; the native renderer evaluates `clip z / w * multiplier + offset`, clamps to 0–255, and blends the source fog color. Missing or mixed per-draw state is rejected. |
| Static vertex lighting | Supported by WSC3 subset | `NUMLIGHT`, bounded hashed/segmented `MOVEMEM G_MV_LIGHT` records, vertex normals, and the pinned SM64/libultraship directional-plus-ambient equation are interpreted at vertex-load time. Since results are static, they are baked into shade RGB; no WSC extension is required. Positional lights, dynamic light changes within a vertex batch, and texgen are not lowered. |
| Projection or non-affine matrices | Rejected | They depend on the runtime camera or projective divide and cannot be lowered to static WSC room vertices. |
| Whole-image texture loads | Supported subset | Existing strict `LOADBLOCK` bounds and format validation. |
| Static `LOADTILE` rectangle | Newly expanded | 4-, 8-, and 16-bit source crops are supported when dimensions, source bounds, resource stride, destination TMEM line, and final WSC extent agree. CI4 crop extraction handles a low-nibble starting texel and repacks high-nibble-first rows. Unsupported alignment/layout is rejected. |
| CI4/CI8 palettes and TLUT | Supported subset | Existing explicit palette-bank/TLUT resolution and RGBA baking. |
| Dual-texture environment-alpha material subset | Supported subset | Existing WSC3 per-material lowering; unsupported equations remain rejected. |
| Filter and axis sampler modes | Supported when equivalent | Existing WSC3 point/three-point and clamp/repeat/mirror modes. Tile mask periods and texture regions that cannot be represented equivalently are rejected. |
| Collision and spawn | Existing supported subset | Validated triangle indices/winding, surface mapping, safe floor spawn, room adjacency and transition links. Water boxes and unsupported collision flags are rejected. |

## Compatibility matrix and open gaps

| Capability | Current status | Why / next requirement |
| --- | --- | --- |
| Fog rendering | WSC4, host-tested subset | Per-material signed fog multiplier/offset and RGBA color are serialized. Runtime fog factor uses the active clip-space `z/w`, matching `gfx_sp_vertex`; shader interpolation/blending follows the existing fog input path. The authored test checks payload decode, shader selection, and CPU factor. Apple Metal compilation and iPhone pixels are unverified. |
| Vertex lighting | Static subset baked into WSC3 | Requires known `NUMLIGHT`, a complete ambient record plus all directional records, and the exact combiner consuming `shade.rgb`. The converter evaluates directional contributions after the same model-view direction transform as `calculate_normal_dir`, adds ambient, clamps RGB, and stores it in vertex color. Authored fixture and source equation coverage pass. Dynamic/positional lighting remains rejected. |
| Generated texture coordinates | Unsupported | Texgen needs transformed normals/geometry and source texture-generation state. |
| Alpha compare / coverage / noise / decal depth | Unsupported unless already equivalent to a WSC3 mode | The importer rejects them; it does not silently map different thresholds or coverage rules. |
| Runtime-generated display lists | Partial | Deterministic layout-supplied pass preludes and existing reconstructed state work. Runtime-dependent calls without an explicit deterministic model reject with command provenance. |
| Scene actors, water, camera scripts, and game events | Not instantiated | WSC3 represents static render/collision rooms, entrances, exits, and adjacency only. |
| Larger-than-package/world bounds | Constrained | WSC and SM64 collision coordinates remain signed 16-bit with the current grid limits. The importer reports a hard failure and requires explicit scale/offset or the opted-in expanded grid. |

This is a format-neutral host conversion boundary into WSC packages, not a claim of full Fast3D or any particular game's scene compatibility. WSC1/WSC2/WSC3 layouts remain unchanged; WSC4 adds only an optional per-material fog record. Static lighting is baked into vertex shade and remains WSC3-compatible. See [`WSC4_FORMAT.md`](WSC4_FORMAT.md).

## Converter report

`tools/o2r_to_wsc3.py ARCHIVE SCENE --dry-run` reports the same validation used by export and does not write output. Its `compatibility` section includes candidate/translated/rejected draw counts, grouped draw and trace failures, batch/material/triangle totals, texture dimensions and decoded RGBA bytes, collision and entrance/link coverage, static matrix resources used, and estimated serialized package/render/collision payload bytes. The estimate deliberately excludes allocator and GPU overhead.

Rejected conversions still include this summary alongside `trace_issues`, `draw_issues`, `scene_issues`, and observed resource paths. Unresolved inputs are not represented as zero work or successful translation.

## Complex original fixture

`tests/test_o2r_to_wsc3.py` builds temporary, authored O2R resources for a two-room connected scene. It exercises opaque and translucent draws, CI4/CI8 material state, two texture inputs, TLUT decoding, cropped 16-bit and odd-nibble-origin 4-bit `LOADTILE`, hashed and segmented matrix references, a pushed translation matrix, collision on both rooms, spawn validation, and the room seam. The combined CI4-plus-matrix fixture converts 4/4 candidate draws into 2 rooms, 6 render triangles, 4 collision triangles, 2 materials, 2 textures, and 1 room link; its WSC3 payload is 1,006 bytes. No game-derived scene or texture is checked into the repository.

The trace-specific authored cases verify fog coefficient/color and vertex-load timing, static `MOVEMEM` light resolution, and the lighting inputs. Conversion tests verify WSC4 fog serialization and WSC3 static-light baking. A native host replay verifies WSC4 material decoding, shader feature selection, fog-factor computation, and scene teardown. The reference parity scripts are sanitizer-instrumented and compare supported tile/TLUT and pass-setup behavior against pinned upstream checkouts.

Run the focused checks:

```bash
python3 tests/test_o2r_draw_trace.py
python3 tests/test_o2r_to_wsc3.py
python3 tests/test_o2r_inventory.py
```

## Validation boundary

Host fixture tests prove deterministic parsing, material lowering, geometry baking, package creation, and WSC4 runtime replay. They do not prove compatibility with arbitrary O2R resources, iOS GPU execution, or physical-device rendering. The matrix lowering assumes source matrices are static model/world transforms; a scene with camera matrices in its display lists must be rejected or given an explicit camera-independent conversion policy. The pinned Fast3D tile/TLUT and pass-setup parity harnesses passed with ASan/UBSan; leak detection had to be disabled because LeakSanitizer aborts under this host's ptrace policy. The full Linux suite and WSC runtime host replay pass. No Apple toolchain is installed (`xcodebuild` unavailable), and no iPhone build has been run for Mission 72. The local OoT NTSC 1.0 archive trace still has 1,054 incomplete candidate draws, including missing pass fog setup and six unresolved dynamic calls; no authentic scene export is claimed. The archive and derived assets remain outside the repository.
