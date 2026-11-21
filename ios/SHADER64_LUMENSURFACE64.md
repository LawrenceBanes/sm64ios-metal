# LumenSurface64 reference pack

LumenSurface64 is a single API 2 pack that exercises both the Fast3D material hook and the render graph.

## Material stage

The material shader receives the original post-combiner color and UVs, the imported checker texture, an actual view-space vertex position, and the configured sun direction transformed into that same view space. It derives a flat face normal with `dfdx`/`dfdy` and uses two-sided Lambert response so reversed projected winding does not invert lighting. This is a geometric face-normal approximation, not an engine-provided smooth normal. Invalid positions, degenerate derivatives, or missing sun data preserve a safe ambient/base-color result.

Parameters are shared with the graph:

| Parameter | Range | Use |
| --- | --- | --- |
| Direct light | 0–2 | Material diffuse gain |
| Ambient contribution | 0–1 | Material ambient floor |
| Mario texture blend | 0–1 | Mix the authored checker into character-class material only (class 3); world/object combiner colors are left untouched |
| World shadow strength | 0–1 | Blend native shadow-map visibility into the composite |

## Render graph

The half-resolution compute pass uses the engine's single-sample depth and inverse projection to reconstruct view-space positions. It transforms those positions and reconstructed normals with the captured inverse view into world space, then applies the renderer's existing world-to-shadow basis, ranges, and depth map. Shadow comparison uses a bounded 3×3 PCF kernel with slope-scaled depth bias. A fullscreen pass presents the intermediate surface.

If depth, normals, camera matrices, or the native shadow map are unavailable, the runtime rejects this graph for that frame and uses its existing fallback path. These resources are not fabricated by the pack.

The material input/output and shadow computation are covered by host source/contract checks and Apple MSL compile/link validation. Those checks do not prove GPU execution, shadow-space visual alignment, appearance, or performance. Physical iPhone comparison is still required.
