# LumenSurface64 reference pack

LumenSurface64 is a single API 2 pack that exercises both the Fast3D material hook and the render graph.

## Material stage

The material shader receives the original post-combiner color and UVs, the imported checker texture, an actual view-space vertex position, and the configured sun direction transformed into that same view space. It derives a flat face normal with `dfdx`/`dfdy`, orients that normal toward the camera in view space, then applies one-sided Lambert diffuse. Reversing projected triangle winding therefore preserves the view-facing orientation while surfaces turned away from the sun do not receive diffuse light. This is a geometric face-normal approximation, not an engine-provided smooth normal. Invalid positions, degenerate derivatives, a zero view vector, or missing sun data preserve a safe ambient/base-color result.

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

## iPhone inspection checklist

1. In Graphics, open **SHADER64**, choose **Import ZIP Pack…**, and select the separately distributed `SHADER64-LumenSurface64-0.2.0.zip`.
2. Start in a stock, UV-textured SM64 level. Compare Classic64 and Lumen Surface Lighting while Mario and the nearby terrain are both visible. The pack checker blend is applied to character class 3 only; world and object combiner colors are preserved.
3. Open the pack's Configure screen. Vary Direct light, Ambient contribution, Mario texture blend, and World shadow strength individually. A change should be attributable to each parameter; return each to its default afterward.
4. In Graphics settings, enable directional Sun and shadows and try another sun direction. Check Mario on the ground, a nearby wall/structure, and an unobstructed view to spot shadow-space flips or swimming.
5. Repeat in Hyrule Field or OoT Market only if the corresponding WSC world assets are installed in the app. Compare elevated and ground-level views; watch for wrong-scale fog, incorrect shadow alignment, missing texture, or fallback to the native renderer.
6. Switch back to Classic64 and verify the unmodified native appearance returns. Exercise a level transition and reopen the shader pack before reporting performance or resource-lifetime issues.

The host derivative test approximates `dfdx`/`dfdy` by intersecting neighboring perspective screen rays with representative view-space floor, wall, and slope planes. It checks camera-facing normal orientation and one-sided lighting invariance when triangle vertex order is reversed. It does not execute Metal `dfdx`/`dfdy`; final GPU derivative behavior, image appearance, thermal cost, and frame rate require the physical-device comparison above.
