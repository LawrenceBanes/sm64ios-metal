# Showcase64 visual test pack

Showcase64 is the first cohesive, authored visual direction test for the Mission 81 Shader64 branch. It combines the API 2 Fast3D material hook with an eight-step, half-resolution atmospheric scattering pass and a full-resolution composition pass.

## What it does

- Adds moderate direction-aware diffuse response, sky tint, broad highlights, and a restrained rim response to actual Fast3D material colors using view-space positions and the configured sun direction.
- Reconstructs a camera ray from scene depth, marches eight bounded samples, and tests each sample against the renderer's existing fitted directional shadow map with compact 2x2 PCF.
- Integrates warm scattering and transmittance into a half-resolution RGBA16Float target.
- Composites the low-resolution volume over the original full-resolution scene color so the game's original image detail is preserved.
- Uses a pack-local minimum sun intensity when the global sun slider is zero, so the pack has a visible authored key light under Classic graphics defaults. It does not mutate the saved global graphics settings.

This is a screen-space volumetric approximation driven by the current frame's depth and one bounded shadow map. It is not path tracing, ray tracing, global illumination, or a temporal volumetric solution. It has no history buffer or motion vectors. The integration is bounded to eight march steps and a half-resolution target, but it performs shadow comparisons per step and still needs GPU timing on iPhone.

## Renderer contract change

API 2 adds the optional pass resource `sceneColorSource:true`. It is accepted only on a full-resolution fullscreen pass whose output is `present`, and it cannot be combined with other optional resources. The runtime binds the original scene color at texture/sampler 9. The new `shader64_pass_scene_color` helper receives both the graph input image and the original scene image. Existing API 1, API 2, WSC, and Classic64 contracts remain unchanged.

The intermediate graph node remains a normal image-to-image compute pass. This explicit input lets a low-resolution effect pass coexist with a sharp full-resolution composite rather than forcing the full game image through the half-resolution target.

## Import and device test

Zip the contents of the pack directory, preserving `manifest.json` and `shaders/`:

```sh
cd ios/shaderpacks/Showcase64
zip -r /tmp/SHADER64-Showcase64-0.1.0.zip manifest.json shaders
```

On iPhone, open **Graphics → SHADER64 → Import ZIP Pack…**, select the archive in Files, then select **Showcase Atmosphere**. Compare the same stable outdoor view in Classic64 and Showcase64. Look for sun-facing surface response and warm light shafts through occluders while checking that texture detail stays sharp. Reduce the Atmosphere density or Scattering controls if the result is too strong. Use the existing Classic64 selection to restore the baseline.

## Known limits and validation boundary

- No reliable per-material roughness, metalness, or water tag is exposed, so highlights are broad and generic. Showcase64 does not implement reflections or water-specific shading.
- Volumetrics are screen-space and use a single fitted directional shadow map. Samples outside its fitted bounds are treated as lit; shadow-map geometry coverage and precision are inherited from the native renderer.
- There is no temporal accumulation, volumetric reprojection, or motion-vector input; subtle spatial banding and edge softness are possible.
- Existing API 2 custom graphs replace the built-in final composite, so native AO, bloom, tone mapping, FXAA, sky replacement, and native light-shaft composition are bypassed while this pack is active. The Showcase graph implements its own scattering/composition only; it does not silently claim those other built-in effects.
- Host tests validate contracts, graph structure, resource selection, and shader source intent. Apple MSL compilation is separate from actual GPU execution. Physical iPhone rendering, visual quality, and frame time remain unverified until tested on device.
