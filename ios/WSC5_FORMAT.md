# WSC5 format note

WSC5 extends the WSC3 per-material layout with the WSC4 fog record and a
versioned decal/blend state. WSC1 through WSC4 byte layouts and flag meanings
are unchanged. The file header, room data, vertices, collision, entrances,
exits, texture descriptors, and optional fog tail retain their earlier layout.

## Material flags

The existing `u16` material flags add:

| Bit | Name | Meaning |
| --- | --- | --- |
| 4 | `WORLD_SCENE_BLEND` | Enable native alpha blending and preserve shader output alpha. |
| 5 | `WORLD_SCENE_FOG` | Append the WSC4 fog multiplier, offset, and RGBA color. |
| 6 | `WORLD_SCENE_DECAL` | Apply the backend's native decal depth bias for this material. |

The batch `pass` continues to preserve the source submission list (`0` opaque
list, `1` translucent list). WSC5 separates that ordering from blend state:
an opaque-list decal may blend while remaining in its original opaque-list
position. A blended opaque-list material must also be a decal; translucent-list
materials must blend; decals must blend. This retains the source room-list
ordering for the Hyrule Field decal located in the opaque list. Older WSC
versions continue to require blend state to match their pass bit.

WSC5 rooms allow up to 256 material batches (WSC1 through WSC4 remain capped
at 64). This bounded increase is needed by the traced Hyrule Field room, which
lowers to 202 source-ordered material batches; it does not raise the existing
per-room triangle, package-byte, or state-slot limits.

WSC5 texture descriptors may contain up to 4,096 texels. WSC1 through WSC4
retain their 2,048-texel limit. Width remains bounded by the existing 2,048
dimension limit, and the runtime validates each referenced PNG against the
declared dimensions.

The WSC5 decal flag selects the existing renderer decal mode, which applies
native depth bias. It does not alter geometry depth-test/depth-write flags,
texture alpha, fog, or material color. The converter accepts only the traced
Fast3D opaque-list and translucent-list decal variants whose source state has
depth testing enabled, depth writes disabled, decal z mode, and no alpha
compare.

## Hyrule Field static LOD lowering

The NTSC 1.0 Hyrule Field source has 57 translucent decal draws whose alpha
combiner is `(tex0.a - 0) * primitive_lod_fraction + primitive.a * tex0.a`.
For draws whose display-list `primitive_lod_fraction` is exactly zero, the
converter evaluates this equation exactly to `primitive.a * tex0.a`. Nonzero
or otherwise different LOD equations remain rejected. The native shader also
multiplies vertex alpha, so these draws are accepted only when every referenced
source vertex has alpha 255. Unused sampled-alpha factors are accepted only
when the decoded texture alpha is fully opaque.

This is a static per-draw lowering, not support for animated Fast3D LOD state.
It does not imply general scene compatibility. WSC5 remains readable alongside
WSC1/WSC2/WSC3/WSC4 through the same bounded package loader.
