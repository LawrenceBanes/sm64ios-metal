# Mission 67E render compatibility

The scene provider reads WSC1 and WSC2. WSC1 keeps its room-level render
list. WSC2 lets original scene authors specify multiple material batches
and create separate opaque and translucent lists through the existing
SM64 Gfx interpreter and Metal backend.

## Compatibility matrix

| Capability | Implementation and evidence | Remaining work |
| --- | --- | --- |
| Indexed textures | Authoring tool expands tightly packed CI4 and CI8 through a selected, explicit RGBA5551 or IA88 palette. It validates byte counts and every index. The host replay checks the exact uploaded RGBA8 bytes, including a transparent texel. | Importer-side resolution of palette banks and TLUT changes in source display lists. No OoT asset conversion or runtime TLUT execution is implemented. |
| Material batches | WSC2 supports up to 64 batches per room and 8,192 combined render triangles. Batches can share a named PNG. Invalid image data or mismatched dimensions fail room-package loading. | One texture per batch, repeat sampling, fixed combiner modes. Source draw state may need additional samplers, lighting, multi-texturing, or alpha cutout. |
| Translucent draws | The provider emits separate Gfx lists for SM64's opaque and transparent layers. Host checks show opaque depth writes, translucent depth tests with writes off, and straight alpha from vertex color multiplied by texture alpha when textured. | Author translucent batches back to front within the room's pass. The engine retains submission order across objects and rooms. There is no transparent triangle sort or exact emulation of OoT's blend modes. |
| Runtime draw-state slots | The loader validates room-local slot indices and emits trusted Gfx commands for bounded, frame-driven signed UV scroll and RGBA tint. Host checks cover active and identity slots, pass ordering, and restoration of interpreter state. | Source Kokiri uses generated per-pass segment contents. There is no mapping from its raw segmented calls, no dual-tile scroll, and no general translation of environment-color combiners. WSC2 tint multiplies vertex RGBA. |
| Constant alpha | Existing RDP constant-one detection is preserved. The Linux suite tests both the combiner path and MSL output. The Apple harness emits the constant-one, vertex alpha, and textured alpha variants. | Independent render and blend confirmation on an iPhone. |
| Lifetime | Package deactivation detaches collision and drops CPU texture cache references. Release frees geometry, Gfx, and slot storage. The fixture changes rooms, re-uploads on return, and rejects a forged slot and a wrong-size texture while the old room remains drawable. | Metal keeps GPU allocations in its bounded texture pool and retains resources for in-flight commands. Actual GPU lifetime and memory pressure remain device checks. Live gameplay still keeps the selected room active until teardown. |

## Authoring interface for Mission 67C

Keep the package and texture files under the app's writable scene namespace.
Use `IOS_O2R_SCENE_RUNTIME=1` and `EXTERNAL_DATA=1` for textured packages.
The area-to-package lookup remains `level_id * 256 + area_index`.

- Set `"version": 2` at the scene root. The existing room bounds, neighbors,
  collision, entrances, and exits keep their meanings.
- Each room has a `batches` array. A batch contains `render_vertices`,
  optional `texture` (`name`, `width`, `height`), optional `pass`
  (`opaque` or `translucent`), and optional `state`. A texture name such as
  `scenes/plate.rgba16` resolves to `Documents/gfx/scenes/plate.rgba16.png`.
- A room's optional `states` array contains `scroll` in 1/32 texel per
  rendered frame and `tint` as four bytes. Slot 0 is identity. The first
  array member is slot 1. Rates are signed 16-bit values. Tint alpha scales
  the authored alpha before sampling and blending.
- Optional top-level `indexed_textures` entries reuse the texture descriptor
  fields. Add `index_bits` (4 or 8), `indices` and `palette` as hexadecimal
  byte strings, and `palette_format` (`rgba5551` or `ia88`). CI4 starts
  with the high nibble, and each palette entry stores its high byte first.
  The encoder writes RGBA PNGs under the supplied Documents root.
  Use a distinct asset name for each baked palette variant.

```sh
python3 tools/world_scene_package.py authored.json Documents/scenes/513.wsc \
  --texture-root Documents
```

The WSC2 header is WSC1's header with magic `WSC2`. Each room stores ID and
bounds, then five little-endian u16 counts for neighbors, batches, collision
vertices, collision triangles and state slots. Neighbor IDs and state
records follow the room header. A state record is two signed 16-bit values
and four tint bytes. Each batch begins with six u16 values for pass, state,
triangle count, texture width, height, and name length. The name bytes and
triangle vertex records follow. Collision and entrance/exit records retain
WSC1's layout. This is an authored-package ABI. It cannot execute raw N64
addresses or arbitrary package-supplied Gfx commands.

The iOS render diagnostics, when enabled, report interpreter-side section
4 for opaque and section 5 for translucent package draws. A queued count
is a CPU submission count. Use it together with the existing Metal draw
logs on a device.

## Kokiri handoff

67D's metadata comparison remains valid for the 12 default exit slots.
Global entrance ID zero is valid. Verify the source revision's entrance
table and setup policy before enabling gameplay. Spawn, room transitions,
and stock return travel still need independent validation. A future
authorized importer can target WSC2's material batches after it resolves
textures, palettes and the original per-pass segment effects. None of the
actual OoT scene geometry, textures, or gameplay was converted in 67E.
