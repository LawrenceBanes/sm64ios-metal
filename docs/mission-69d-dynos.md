# Mission 69D DynOS actor checkpoint

This branch extends the Mission 69 runtime with a bounded decoder and native
resource linker for compressed DynOS actor `.bin` files. It is an intermediate
implementation checkpoint. Model IDs are **not** registered yet, and no game
object uses the linked GeoLayout data.

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
texture bytes reaching the renderer adapter. It does **not** verify Metal GPU
pixels, model registration, animation, collision, or iPhone behavior.

The committed synthetic tests cover malformed headers, truncated zlib,
declared-size overflow, unsupported records, unresolved references, cycles,
native array linking, and triangle commands. Third-party actor bytes are not
included in the repository.

## Next implementation gates

1. Validate GeoLayout command control flow before passing imported commands to
   `process_geo_layout`; map only known upstream function IDs to real engine
   callbacks.
2. Register embedded PNGs with the mod-owned texture pool and bind linked
   GeoLayouts to reserved native model slots and CoopDX extended IDs.
3. Detach all object references before freeing a mod's graph, display lists,
   vertices, and textures on disable or reload.
4. Decode and play animation records, then implement dynamic collision.
5. Compile on iPhoneOS arm64 and test a real actor's pixels and lifecycle on
   physical hardware.

Upstream references: [actor record reader](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_actor.cpp), [compression](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_compress.cpp), [pointer sidecars](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_pointer.cpp), [vertex encoding](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_bin_vtx.cpp), and [model pool](https://github.com/coop-deluxe/sm64coopdx/blob/main/data/dynos_mgr_models.cpp).
