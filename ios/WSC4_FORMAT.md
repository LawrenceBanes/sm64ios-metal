# WSC4 format note

WSC4 is a backward-compatible extension of WSC3 for one demonstrated source
state that cannot be preserved by baking: camera-depth fog. The file header,
room records, collision, entrances, exits, batch ordering, and texture records
are byte-for-byte WSC3-compatible. A WSC4 file uses `WSC4` magic and the WSC3
material/room layout.

## Material extension

The existing `u16` material flags gain bit 5 (`WORLD_SCENE_FOG`). When clear,
the material ends after the two existing texture descriptors exactly as in
WSC3. When set, WSC4 appends this little-endian record immediately after those
descriptors:

| Field | Encoding | Meaning |
| --- | --- | --- |
| multiplier | `s16` | Signed Fast3D `G_MW_FOG` multiplier |
| offset | `s16` | Signed Fast3D `G_MW_FOG` offset coefficient |
| color | 4 bytes | `SETFOGCOLOR` RGBA; RGB is used for the fog blend |

The WSC loader accepts the flag only in WSC4 and bounds-checks the appended
record. WSC1/WSC2/WSC3 readers and layouts are unchanged. The Python encoder
rejects `fog` material fields for versions earlier than 4.

## Runtime semantics

For each submitted scene vertex, after the active SM64 transform produces clip
coordinates `z` and `w`, the renderer computes the same factor as
`gfx_sp_vertex`:

```text
if abs(w) < 0.001: w = 0.001
winv = 1 / w
if winv < 0: winv = 32767
factor = clamp(z * winv * multiplier + offset, 0, 255) / 255
```

The vertex stream carries `(fog RGB / 255, factor)`. The existing Metal scene
material shader perspective-interpolates this value and blends only RGB:
`mix(surfaceRGB, fogRGB, factor)`. Surface alpha, blending, cutout, and depth
state remain governed by the pre-existing material/pass fields.

## Static vertex lighting

Static, resolved directional plus ambient RSP lighting does **not** need a new
WSC field. The importer applies the pinned SM64 equation at conversion time
when `G_LIGHTING` is active and the combiner consumes `shade.rgb`: transform
each light direction with the source model-view 3x3, normalize it, accumulate
positive `dot(normal, direction) / 127` contributions, add ambient RGB, clamp,
and bake the result into the existing vertex shade color. This path remains
WSC3. It rejects missing light records and unsupported light counts rather
than substituting source vertex colors.

## Explicit limits

- A draw must have one complete fog coefficient state and one fog color; mixed
  fog-enabled/disabled loads or multiple coefficient states in a draw are
  rejected.
- Fog is evaluated from the current runtime clip `z/w`; no camera transform is
  baked into the package.
- Static lighting supports the current directional-plus-ambient subset only.
  Positional lighting, light animation/state changes within one vertex batch,
  and generated texture coordinates are not represented by this change.
- Fog RGB-only behavior is based on the native SM64 vertex-fog path; the fog
  color alpha is retained in the package for provenance but does not replace
  surface alpha.
- Linux host replay validates decoded fields, the selected shader variant, the
  generated fog factor, and lifecycle. Apple Metal compilation and device
  pixels remain unverified for this Mission 72 branch.

See `MISSION_72_COMPATIBILITY.md` and `MISSION_72_REPORT.md` for the current
validation boundary.
