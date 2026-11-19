# Mission 67H.4 — WSC3 materials and diagnostic room

WSC3 adds a format-neutral material path to the existing SM64 room provider and Metal backend. The package writer accepts original authored geometry, collision, room links, entrances and RGBA or indexed texture inputs. It remains usable for scenes with zero or one texture and supports a second texture with an independent UV transform and sampler. WSC1 and WSC2 retain their binary layout and draw paths.

## Binary format

All integers are little-endian. WSC3 uses the WSC2 header, room records, neighbor lists, vertex records, collision records, entrances and exits. The four-byte magic is `WSC3`. The WSC2 room-state count is reserved and must be zero. Each batch replaces the WSC2 material prefix with the following fields, followed by three existing render vertices per triangle.

| Fields in byte order | Type | Meaning |
| --- | --- | --- |
| Pass, triangle count, flags | Three u16 | Pass 0 is opaque or cutout and pass 1 is translucent. Flag bits 0, 1, 2, 3 and 4 select depth test, depth write, cutout, backface culling and blending. Other bits are invalid. Blend must match pass 1 and cannot accompany cutout. |
| Primitive and environment | Eight u8 | RGBA multiplier and environment RGB multiplier. Environment alpha selects the blend between samples. |
| Environment period, peak | Two u16 | Period zero selects fixed alpha. Periods of two or more animate base to peak and back over rendered frames. Peak must fit in u8. |
| Texture binding 0, then 1 | Two binding records | A zero-size, zero-length record is absent. Binding 1 requires binding 0. |

Each binding has width, height and packed sampler as u16, then S/T scale, origin and scroll as three pairs of s16. A u16 name length precedes the name bytes. Names are relative `scenes/*.rgba16` texture identities resolved as `gfx/scenes/*.rgba16.png`. The sampler uses bits 0–1 for S, bits 2–3 for T, and bit 4 to select three-point filtering. Axis modes 0, 1 and 2 select repeat, mirror and clamp. The remaining values are rejected.

UV scale is signed Q8.8. Origin and scroll use signed 1/32-texel units, with scroll applied per rendered frame. Both bindings start from the vertex UV. Repeat and mirror scroll reduce by the appropriate period. Clamp scroll uses the accumulated position. Three-point filtering uses the existing Metal kernel and half-texel bias. Image dimensions follow the existing WSC limit of 2,048 pixels per binding and width divisible by four.

The shader multiplies vertex and primitive RGBA by the chosen sample and environment RGB. With two textures the sample is `mix(tex0, tex1, environmentAlpha)`. Without textures the sample is white. Cutout discards alpha below 0.5. Opaque output is alpha one. Translucent output uses the existing Metal straight-alpha pipeline. Draws retain source order within each pass. Fog, arbitrary combiners, primitive depth, custom blend equations, per-tile N64 masks and scene-layer/event logic are outside this format. The authoring tool rejects unknown material fields.

The provider validates texture PNGs at load and uses the existing render-state cache and room lifetime. Both texture identities are invalidated at unload. The display-list interpreter flushes at material boundaries and restores the ordinary SM64 render state when leaving the scene list. Backends must explicitly advertise WSC3 support. The Metal shader attribute layout and descriptor use the same definition.

## Original diagnostic and local packages

Set `IOS_O2R_SCENE_RUNTIME=1 IOS_SCENE_DIAGNOSTIC=1 EXTERNAL_DATA=1` in the existing iOS project. The single diagnostic switch bundles both original rooms. In active Castle Grounds gameplay, open Gameplay and choose **Enter WSC3 Diagnostic**. The left panel combines two moving patterns with independently configured addressing and a cycling environment blend. The right overlaps an opaque backdrop, a cutout panel and a translucent panel. Walk over the floor and ramp. Use **Leave WSC3 Diagnostic** to return and repeat. The WSC2 entry remains available.

To author a compatible package locally, use `python3 tools/world_scene_package.py scene.json Documents/scenes/<id>.wsc --texture-root Documents`. Create the `scenes` output directory first. The encoder writes authored indexed textures under `Documents/gfx/scenes/`. For RGBA textures supplied separately, copy the referenced PNGs there at the declared dimensions. Existing areas look up the package ID `(level << 8) | area` and entrance 0 at area load. IDs 4098 and 4099 in the diagnostic build are reserved for its bundled fixtures. There is no arbitrary package browser or automatic warp registry. Choosing an active area key affects that area on entry, so use a dedicated test area while developing custom scenes.

## Kokiri compatibility

The earlier private metadata trace established 425 effective dual-input environment-alpha draws. WSC3 represents that blend and independent per-binding transforms. The trace still shows source fog on all 1,054 candidates, source lighting at vertex load on 1,052, alpha/coverage behavior outside a normal opaque pass on 168, and nonpreset depth state on 25. WSC3's fixed cutout is a known diagnostic behavior. Pixel parity with those source modes remains unverified. The real archive has no converted package in this mission. Room/entrance behavior and sprite/background modes need separate validation.

## Local verification

- Host replay exercised the production WSC1/WSC2/WSC3 readers and collision registration, opaque/cutout/translucent ordering, independent UVs, sampler state, environment animation, exit and re-entry. Unsupported material and backend cases were rejected.
- The shader tests validate dense indices, stride, and generated source for the two-UV shaders. The Apple shader emitter includes the WSC3 blend and cutout variants.
- Full Linux checks and ASan/UBSan scene-package tests passed. Apple and iPhone results are reported separately from the host replay.
