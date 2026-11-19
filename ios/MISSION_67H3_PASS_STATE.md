# Mission 67H.3 — Kokiri pass state and WSC2 compatibility

The host tracer now initializes each normal room pass, executes modeled runtime segment lists, and classifies each candidate draw against WSC2. It reads the archive locally and writes metadata. The converter, WSC formats, native renderer, and Apple build inputs are outside this change.

## Reproduction

Run from the repository root with the local OoT NTSC 1.0 archive. The context arguments are gameplay frame, scene layer, event flag 07 (0 or 1), and signed room draw parameter 0.

```sh
python3 tools/o2r_draw_trace.py "$HOME/Downloads/oot.o2r" \
  scenes/shared/spot04_scene/spot04_scene \
  --kokiri-context 0 0 0 0 --lower-wsc2 \
  --output build/mission67h3/kokiri-frame0.json
```

Run the same command again with another output path and compare with `cmp`. Use `--kokiri-context 37 6 1 150` for the layer-6 case. These inputs describe reproducible scenarios. They are independent of an actual save, entrance setup, room-transition overlap, or recorded frame. The CLI checks the resource name and NTSC 1.0 version marker `EC7011B7`. The archive SHA-256 used here was `e3bf3ae3565768998026e60113ed3fc506fc5415e23299de41b9b42a5fbc3807`.

## Reconstructed state

| State | Modeled behavior | Evidence |
| --- | --- | --- |
| Room setup | Scene setup precedes current and previous room submission. Each isolated opaque and translucent room pass starts with `SETUPDL_25`. Opaque room type 3 disables color dithering after setup. Each of the three checked room headers declares room type 0. | [Play_Draw](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_play.c#L1330-L1343), [Room_DrawNormal](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_room.c#L76-L113), [opaque setup wrapper](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_rcp.c#L1055-L1084) |
| Texture, geometry, and combiner | Texture on at full S/T scale, base tile 0, LOD level 0. Geometry enables Z-buffer, smooth shade, backface culling, fog, and lighting. Two cycles begin with texture × shade and texture alpha, then primitive modulation. | [SETUPDL_25](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_rcp.c#L298-L308) |
| Render modes | Both pass defaults enable perspective and bilerp, disable RDP TLUT and alpha comparison, select pixel depth with compare and write, and set fog/opaque blend inputs. Commands inside room lists can override the modes. | [GBI constants and render modes](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/include/ultra64/gbi.h#L464-L592) |
| Environment, primitive, and segment scopes | Scene config sets both passes' initial environment color to RGBA `128,128,128,128`. Primitive color and LOD data come from decoded commands when referenced. The opaque pass binds `0x0a/0x0b/0x0c`, and the translucent pass binds `0x08/0x09/0x0b`. Scene layer, event 07, and room parameters drive the generated environment alpha. | [Scene_DrawConfigKokiriForest](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_scene_table.c#L1176-L1232), [cutscene-layer rule](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/include/save.h#L399-L404) |
| UV scroll | The generated lists emit tile sync and size commands for two independent tiles. The model uses the source unsigned wrapping, 10.2 fields, extents and conversion of the signed room input. | [Gfx_TwoTexScroll](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/src/code/z_rcp.c#L1450-L1466) |
| Fast3D interpretation | Raw muxes remain in the trace. Effective expressions account for the one-cycle texel alias, second-cycle binding swap, cancelled terms and discarded cycle outputs. CI palette state records both the raw RDP TLUT interpretation and the pinned Fast3D RGBA5551 decode. | [GenerateCC](https://github.com/Kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/interpreter.cpp#L195-L395), [CI4 and CI8 imports](https://github.com/Kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/interpreter.cpp#L972-L1179) |

Segment 3 points to room memory in OoT. The archive stores separate resources, so the tracer resolves hashed resources and explicit aliases without inventing a segmented ROM base. The profile runs independent room snapshots. It leaves camera matrices, actual light and fog parameters, visibility, and inherited previous-room data outside its evaluation. Those separate pipeline inputs are needed for a pixel comparison. Copy/fill cycles, unsupported commands, malformed offsets and missing material inputs retain explicit diagnostics.

## Authentic trace results

| Measure | Mission 67H.2 | Reconstructed frame 0 |
| --- | ---: | ---: |
| Room headers | 3 | 3 |
| Decoded display lists | 40 | 40 |
| Decoded display lists without issues | 33 | 40 |
| Opaque candidate draws | 1,009 | 1,009 |
| Translucent candidate draws | 45 | 45 |
| Candidates with all interpreted inputs resolved | 0 | 1,054 |
| Fully verified draws | 0 | 0 |
| Approximate draw records | 0 | 1,054 |
| Incomplete draw records | 1,054 | 0 |
| Unresolved generated calls | 6 | 0 |

The 1,054 candidates contain 2,060 triangle primitives. The new trace processed 3,007 commands compared with 2,947 in the H.2 trace. All six generated calls resolved. The four opaque calls use 0x0a, 0x0b twice, and 0x0c. The two translucent calls use 0x08 and 0x09. All active combiner color, palette-reference and tile inputs in the modeled subset resolved. The GBI rasterization and image data were outside the check. All draws retain the Fast3D `CULLDL` visibility approximation. An additional 472 draws use the RGBA5551 CI decode despite the raw disabled TLUT mode.

The frame-37 scenario leaves those counts stable and changes the UV metadata of 162 draws. An environment-alpha source changes from 50 to 65 on 230 draws that actually reference it. That state would be frozen by baking one scene snapshot. The two frame-0 runs produced byte-identical JSON with SHA-256 `d951e9e69c7c032dd5de826c2b723250c784842f492b1ad9a1741ad694179df8`. Each private trace took under three seconds on this Linux host, with roughly 70 MiB peak resident memory.

## WSC2 lowering

| Classification | Opaque | Translucent |
| --- | ---: | ---: |
| Exactly representable | 0 | 0 |
| Representable with validated baking | 0 | 0 |
| Representable only with documented approximation | 0 | 0 |
| Requires a versioned capability extension | 1,009 | 45 |
| Blocked by unresolved modeled source state | 0 | 0 |

Each draw can carry several independent mismatch reasons. WSC2 uses fixed shade or single-texture modulation, repeat sampling, two fixed passes, per-channel vertex tint, and a single signed per-axis scroll slot. The harness checks those capabilities against the raw state and effective expression. It checks texture extents and byte ranges, reports pending TMEM/PNG or UV proofs, and records conditional inheritance of the WSC2 texture filter. The actual package's per-room batch and memory limits need an exporter-level test.

| WSC2 gap | Evidence in the checked draws |
| --- | --- |
| Dual texture, independent UV and environment alpha | 425 effective RGB expressions blend the two inputs using environment alpha. Of those, 406 bind distinct image paths and 352 have differing sampled UV descriptors. The input changes across scenarios on 230 draws. |
| Per-axis sampler behavior | 385 draws use clamp, mirror, or a mask period that differs from full-resource repeat. Without independent UV-range and sampler-edge validation, repeat is an approximation. |
| Fog and culling | The pinned blender requests fog in all 1,054 draws. 992 draws enable culling. WSC2 clears the corresponding geometry state and supplies no source fog data. |
| Alpha, depth, and ordering | 168 opaque candidates request alpha/coverage behavior outside the opaque WSC2 pass. 25 draws use a depth configuration outside the two WSC2 pass presets. Opaque/translucent source order within each room is recorded. |
| Texture budget | 36 draws reference a backing image over WSC2's current per-material size limit. A crop or split would need a seam and sampler proof. |
| Lighting, static colors, and texture conversion | 1,052 use source lighting at vertex load. Authored RGBA, CI4/CI8 palette bakes and static UV conversion are covered by fixtures. Actual lighting inputs, pixel equality, and texture footprint across all rooms remain unvalidated. |

A scoped versioned material record is justified for the observed dual-input blend. Prototype two texture references with independent UV and sampler settings, a small supported combiner that includes shade and runtime environment-alpha interpolation, and explicit pass/depth/coverage and fog/culling state. Keep layer/event-dependent environment alpha and animated tile origins dynamic where the trace shows them changing. Check whether fixed primitive-color factors and static lighting can be baked without quantization or runtime changes before adding fields for them. Use a separate magic/version and validate indices, sampler enums, pass masks and payload lengths. WSC1 and WSC2 readers should keep their present contracts. The evidence does not require a general microcode VM or an arbitrary combiner program in the file format.

The recommended next milestone is a native authored fixture implementing the measured two-input blend, including a moving tile origin, alpha coverage and fog. A side-by-side comparison against the reference renderer with known inputs would establish material correctness before integrating more room data.

## Trace format and validation

JSON version 2 retains provenance and raw state, adds the optional `profile`, `generated_lists`, `generated_calls`, normalized `combiner.effective`, and optional `wsc2` summaries. Generated records carry source links and in-list offsets. Draw rows preserve source order and identify room, pass, display-list resource, source offset, vertex reference ranges, texture bindings and palettes, raw and effective combiner, and render state. The illustrative effective-state fragment below describes an authored test case.

```json
{
  "inputs": ["environment.a", "shade.rgb", "tex0.rgb", "tex1.rgb"],
  "rgb": ["multiply", "shade.rgb", ["combine", "tex1.rgb", "tex0.rgb", "environment.a", "tex0.rgb"]],
  "texture_inputs": [0, 1]
}
```

- **PASS** Complete Linux checks with Clang, including 101 Python unittest cases and the native WSC1/WSC2 package, collision and room-change recovery checks. The existing `ACT_GROUND_POUND` comparison warning remains.
- **PASS** Pinned macro/scroll reference harness expanded the real `SETUPDL_25` macros and executed the original dual-tile helper for scrolling and signed-origin cases under ASan/UBSan. The pinned Fast3D descriptor/bank harness and native WSC package replay also passed under ASan/UBSan.
- **PASS** Eleven new authored Python fixtures checked pass overrides, generated call provenance, failure cases, combiner aliases, disabled TLUT interpretation, indexed texture baking, exact UV range checks, and all five WSC2 verdicts.
- **UNVERIFIED** Apple build and device behavior for this host-only change. The earlier H.2 Apple validation ran at `0e66d388`. No Apple build paths changed in this mission. [Mission 67H.2 validation run](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44421679105)
