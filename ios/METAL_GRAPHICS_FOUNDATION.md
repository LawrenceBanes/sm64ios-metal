# Configurable Metal graphics foundation

## Compatibility baseline

`ios-classic-53.1-device-confirmed` tags the source tree used by the visually confirmed Build 53.1 IPA. Its tree matches the Build 53.1 source snapshot. Classic is the default and keeps rendering directly into a fresh `CAMetalLayer.nextDrawable`, uses one sample per pixel, native drawable resolution, and anisotropy 1. The display-list interpreter, texture upload format, UV conversion, and texture sampler address translation remain on the existing path.

## Settings and fallback

The native diagnostics overlay's **Graphics** button opens settings for Classic/Enhanced, 50/75/100 percent render scale, MSAA Off/2x/4x, anisotropy Off/2x/4x/8x/16x, live CPU/GPU/FPS/draw/triangle/resolution readings, and reset. Values persist in `NSUserDefaults`. A choice is normalized against `MTLDevice.supportsTextureSampleCount:` and sampler creation capability. When a target or final-pass resource cannot be created, the backend logs the reason and returns to the direct drawable at 100 percent and 1x; it does not stop gameplay.

Enhanced defaults to full resolution, 4x MSAA when available, and anisotropy 8x. Render scale and sample count are independent: any setting requiring a scaled or multisampled target uses the offscreen path. Anisotropy is a sampler-state limit and only affects a texture while that texture's existing sampler uses linear filtering. Existing repeat, mirror-repeat, and clamp-to-edge behavior and texture upload/UV semantics are unchanged.

## Render-target flow

The game pass uses an internal color target and size-matched depth target only for non-native resolution or MSAA. MSAA color is resolved once into a single-sample texture. One full-screen Metal pass writes that image into the frame's already acquired drawable, which is then presented and committed through the established layer path. A 100-percent, 1x configuration bypasses both internal targets and the copy pass. Targets are cached by dimensions and sample count and recreated after resize or settings changes. Metal retains encoded resources for in-flight work; no per-draw or per-frame GPU wait was introduced.

The sample-count-aware pipeline cache retains the existing 16 depth/blend/decal state variants per sample count. Allocation, sample-count, or final-pass failures select the direct path. Actual visual quality and memory pressure still need device testing; 4x full-resolution targets use more transient GPU memory than Classic.

## Measurements

- **CPU frame time** is wall-clock duration around one game tick, including game logic, audio generation/submission, and graphics submission.
- **FPS** is an exponentially smoothed estimate from render-frame start intervals.
- **GPU time** uses Metal command-buffer GPU start/end timestamps only when the completed command buffer reports valid values. Otherwise the panel displays that GPU timing is unavailable. The interval includes encoded game and final-presentation passes; it is not display latency.
- Draw calls and triangles count successfully encoded SM64 triangle batches for the current frame. Resolution and sample count are the active game-target values.

## Future extension points

The offscreen color target and final-pass encoder are the extension seam for post-processing. No bloom, tone mapping, screen-space reflections, or other placeholder effects are implemented. Shadow maps need light-space transforms and world-space casters; ambient occlusion and screen-space reflections need meaningful depth and usually normals; physically based lighting and ray tracing need stable world-space geometry, transforms, and material records. `gfx_scene_capture` is the intended source for that scene data. Hardware ray tracing and a software BVH path remain separate future projects with runtime capability checks.

## Validation boundary

Portable settings, sample fallback, dimensions, and pipeline cache indexing are covered by `tests/metal_state_test.c`. The exact runtime final-pass MSL is emitted by `tests/metal_shader_emit.c` and compiled into AIR/metallib by the Apple validation workflow. Source checks cover target configuration, resolve, direct fallback, persistence, and the UIKit controls. Linux cannot compile Objective-C Metal code or run GPU work; Apple SDK compilation proves API/shader compatibility, not correct rendering. A device run is still required on A16, with A18 Pro and M1 iPad validation also outstanding.
