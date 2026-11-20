# Mission 69D DynOS actor checkpoint

This branch extends the Mission 69 runtime with a bounded decoder and native
resource linker for compressed DynOS actor `.bin` files. It is an intermediate
implementation checkpoint. Bounded object GeoLayouts now register real model
IDs and graph nodes. OMM's six sparkle actors and Peach's `omm_peachy_geo`
pass validation and native graph construction in the host runtime; other
custom actors still fail with precise diagnostics.

## Format and bounds

The decoder accepts the upstream `DYNOSBIN` signature, a little-endian 64-bit
uncompressed length, and one zlib stream. Upstream's actor format does not
carry a separate version field. Input is capped at 8 MiB and decompressed data
at 16 MiB. A bundle may contain at most 4,096 records, 32,768 references,
262,144 words per record, and 4 MiB per embedded PNG. Truncated streams,
trailing compressed bytes, unknown records, unresolved names, cyclic native
display-list/geolayout references, and unsupported pointer uses fail with an
offset or resource name. The decompressor checks the declared decoded length.

Parsed record types are `LIGHT` (1), `TEXTURE` (2), `VERTEX` (3),
`DISPLAY_LIST` (4), `GEO_LAYOUT` (5), `ANIMATION` (8), `ANIMATION_TABLE` (9),
`LIGHT_T` (18), `AMBIENT_T` (19), `TEXTURE_LIST` (20), and `LIGHT_0` (24).
The native linker currently materializes types 1–5, 18, 19, and 24. It
converts packed 16-bit or float-sentinel vertices to this port's `Vtx` and
resolves pointers into native `Gfx`/`GeoLayout` arrays. Animation and texture
list records are parsed but **not activated**. Native linking requires a real
texture registrar and function resolver from its caller; missing resources
cause failure. It does not execute functions from the bundle.

The production runtime registers embedded PNGs in the mod-owned texture pool,
maps the upstream function indexes used by Peach (0, 2, 4, 5, 7, 8, 9, 10,
12, 14, 15, and 37) to matching native engine callbacks, validates every
GeoLayout command stream in the actor, calls the existing
`process_geo_layout`, and allocates an extended model ID with a reserved native
model slot (`0xE6`–`0xFF`). The accepted root has a 256-word and 64 KiB graph
pool budget. `obj_set_model_extended`, spawning, and model equality use the
registered graph. Disable detaches object graph pointers before model/texture
freeing; re-enable rebuilds all resources. The decoded actor byte budget is
16 MiB per mod and 64 MiB globally, in addition to texture budgets.

## Pointer and GeoLayout cross-check

Compared against [PeachyPeachSM64/dynos-decomp](https://github.com/PeachyPeachSM64/dynos-decomp): its `read_pointer` interprets `FUNC` as an index into its ordered function-name table and `PNTR` as a named record plus an offset. GeoLayout words are little-endian 32-bit words; the interpreter-facing C output restores typed pointers. Its GeoLayout table defines branch-and-link, branch/return, node nesting, display-list, shadow, ASM callbacks, switch cases, animated parts, held objects, and the `0x10` translate/rotate family. It is a decompiler/reference, not a runtime or proof that arbitrary functions are safe.

Our reader already preserved `FUNC` indices and `PNTR` offsets, but activation
had only two function mappings and rejected nonzero Gfx/GeoLayout offsets. The
native linker now applies those offsets as typed array-element offsets for
vertex, display-list, and GeoLayout pointers, while still rejecting invalid
targets and nonzero offsets into renderer-owned texture handles. The GeoLayout
validator now accounts for the relevant branch/return, shadow, ASM, switch,
held-object, and translate/rotate commands and validates all GeoLayout records
before graph construction. Peach's top layout begins with a shadow and scale,
then callback nodes, and only later creates `GEO_NODE_START`; requiring the
first word to be that root was incorrect. The OMM Peach actor uses function
indexes 0, 7, 12, 37, and 4 in addition to the other Mario-part callbacks in
its child layouts. These are resolved only to corresponding native engine
functions; unknown indices still fail closed.

## Authentic actor probe

`MOD_EXTERNAL_ZIP=/path/to/authorized-original.zip bash
tests/mods/run_dynos_actor_tests.sh` extracts actor bundles only to a temporary
test directory. The original OMM Rebirth source ZIP at upstream commit
`536b5ed0bc88fd777250d3701cb876029582a2a1` supplied 13 `.bin` files.
All 13 decompressed, parsed, resolved their references, and linked native
vertex/display-list/geolayout arrays. The largest actor, `omm_peachy_geo`,
contained 174 records, 580 references, and 3,768 triangle commands.

With that private ZIP supplied to `tests/test_hyrule_render_replay.py`, the
first original OMM sparkle actor submitted two triangles and uploaded one
decoded embedded PNG through the production display-list interpreter's
graphics API boundary. This verifies native host rendering commands and
texture bytes reaching the renderer adapter. The production mod importer then
registered all six original OMM sparkle actor graphs, assigned extended model
IDs `65536`–`65541`, detached a Mario object pointer on disable, and rebuilt
the six graphs on re-enable. A host-only probe then asked the production model
loader to register the authentic `omm_peachy_geo` bundle, exercising its 53
GeoLayout streams and native graph construction; the host harness supplies
inert callback functions, so this does not verify callback semantics or the
original OMM scripts invoking the lookup. This does **not** verify Metal GPU
pixels, gameplay animation, collision, or iPhone behavior.

The committed synthetic tests cover malformed headers, truncated zlib,
declared-size overflow, unsupported records, unresolved references, cycles,
native array linking, and triangle commands. Third-party actor bytes are not
included in the repository.

## Next implementation gates

1. Resolve the remaining GeoLayout opcodes/functions and actor structures used
   by OMM Tiara and star actors. No partial graph is registered when an opcode
   or function is unsupported.
2. Exercise original OMM object spawning with its dynamic IDs and verify the
   graph-to-Metal draw path on iPhone. Host tests currently replay an authentic
   display list at the renderer boundary and register graphs separately.
3. Decode and play animation records, then implement dynamic collision.
4. Resolve OMM's zero HUD texture draws in the tested synthetic host frame.
5. Compile on iPhoneOS arm64 and test a real actor's pixels and lifecycle on
   physical hardware.

Upstream references: [actor record reader](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_actor.cpp), [compression](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_compress.cpp), [pointer sidecars](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_pointer.cpp), [vertex encoding](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_vtx.cpp), [model pool](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_mgr_models.cpp), and the decompiler's [pointer decoder](https://github.com/PeachyPeachSM64/dynos-decomp/blob/master/src/read.py), [GeoLayout records](https://github.com/PeachyPeachSM64/dynos-decomp/blob/master/src/structs/geolayout.py), and [GeoLayout command definitions](https://github.com/PeachyPeachSM64/dynos-decomp/blob/master/src/write/geolayouts.py).
