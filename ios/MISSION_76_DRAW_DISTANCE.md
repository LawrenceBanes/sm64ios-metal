# Mission 76 — Imported Hyrule Draw Distance

## Visibility audit

- The Castle Grounds camera graph supplies a 20,000-unit far plane. `geo_process_perspective()` forwards that value to `guPerspective()`, which feeds the active renderer projection.
- `obj_is_in_view()` has a separate 20,000-unit center-distance cutoff for graph objects. This check is distinct from static WSC room drawing.
- The imported WSC room path submits each active region's display list from the camera graph; it does not evaluate the original OoT culling spheres. The converter retains source polygon lists, so source OoT distance culling is not responsible for this cutoff.
- The imported package loader keeps its scene regions resident. Room residency is not the observed static-world visibility limit in this path.
- The iOS Metal backend uses a 32-bit floating-point depth attachment and normalized projection depth. The near plane is unchanged when extending the far plane.
- WSC fog is material-controlled, with coefficients operating on normalized clip depth. The new fog toggle changes the WSC shader/material path independently of projection distance. Ordinary SM64 materials are unaffected.
- Imported translucent materials keep their existing blend/layer setup. No new sorting policy was introduced; extending visibility can raise GPU work for farther visible geometry.

The source identifies the stock projection far plane as a confirmed visibility bound. The user-referenced iPhone recording was not available in this work session, so the visible cutoff in that specific recording and the perceived improvement are not independently confirmed here.

## Runtime settings

In the iOS developer gameplay menu, **Imported world view settings** provides:

| Preset | Imported-scene camera far plane (with the current 20,000-unit camera) |
| --- | ---: |
| Original / compatibility | 20,000 |
| Extended (default) | 35,000 (1.75×) |
| Maximum practical distance | 60,000 |

The settings only alter active imported WSC scenes. Normal SM64 level projections retain their source far plane. Object graph culling is extended for imported worlds but capped at 32,760 units to stay below the signed 16.16 transform conversion danger range; static WSC geometry follows the camera frustum farther than that. The setting and fog toggle persist in `NSUserDefaults`.

The display-distance unit is the WSC/SM64 world unit. Hyrule's 1.75× scale is baked into the package's render, collision, and entrance coordinates; it is not applied again at runtime. Fog distances are clip-space coefficients and are not multiplied by the world scale.

## Validation and limitations

`tests/run_world_scene_view_test.sh` checks preset selection/scope, object culling bounds, perspective depth endpoints and ten-unit depth separation at both extended distances, and fog independence. The full Linux suite compiles the imported scene render path with the new helper.

This environment did not run the game on an iPhone or record an elevated-camera comparison. Consequently no device draw counts, triangle counts, frame times, or visual artifact observations are claimed. The existing iOS Graphics diagnostics panel reports draw calls, triangles, CPU/GPU frame time, render dimensions, MSAA and anisotropy; compare the Original, Extended, and Maximum presets from the same elevated viewpoint. Increased geometry visibility can cost GPU time, so Extended is the default and Maximum should be chosen based on that on-device comparison.
