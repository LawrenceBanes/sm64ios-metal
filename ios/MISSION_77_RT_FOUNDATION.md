# Mission 77 — Native Metal Ray-Tracing Foundation

## Branch and baseline

Development is isolated on `mission-77-native-metal-raytracing`, based on Mission 76 checkpoint `7c57539c85ccdd14734f83b994a894cbe197dc8e`. Mission 76 remains its own branch/worktree.

## Pipeline audit

- `src/pc/gfx/gfx_metal.m` owns the `MTLDevice`, command queue, main render encoder, game color/depth targets, draw counters, and GPU command-buffer timing. The native view currently renders BGRA8 and Depth32Float.
- The SM64 RSP interpreter in `src/pc/gfx/gfx_pc.c` transforms vertices on the CPU through model-view and projection matrices. The Metal draw interface receives clip-space vertex positions plus material varyings; it has no world-position stream or acceleration-structure input.
- `gfx_scene_capture` can retain local vertices, normals, object transforms, and camera matrices for a bounded capture, but it is currently a separate CPU capture/BVH subsystem. It captures up to 65,536 triangles per frame and does not itself build a Metal acceleration structure.
- The renderer supports direct drawable rendering and an optional offscreen color/depth plus final presentation pass. A shadow effect needs an intentional world-space geometry path and a defined raster/compute/composite sequence; simply adding an RT shader to the current combiner source would not have ray geometry to intersect.
- Dynamic actors are submitted through the same display-list pipeline as level geometry. Static and dynamic geometry need separate lifetime/update rules before a scene AS can be considered correct.

## Runtime capability reporting

The new diagnostic queries `MTLDevice.supportsRaytracing` and `supportsRaytracingFromRender` when the OS exposes them; it never infers support from the device name. It also reports whether the device supports Apple GPU family 9/10, render-target allocation for Depth32Float and RGBA16Float, unified memory, `os_proc_available_memory()`, `recommendedMaxWorkingSetSize`, `currentAllocatedSize`, and `maxBufferLength`.

Apple’s current feature tables list ray tracing in compute pipelines for Apple GPU family 6 and later, while hardware acceleration is identified for Apple family 9. This is a capability distinction, not a claim about the A16, M1, or M4 physical device in this build. The same runtime check will select a supported path on each device. See [Metal Feature Set Tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf), [MTLDevice ray-tracing support](https://developer.apple.com/documentation/metal/mtldevice/supportsraytracing), and [Apple’s Metal ray-tracing sample](https://developer.apple.com/documentation/metal/accelerating-ray-tracing-using-metal).

## Current milestone state

Implemented in this checkpoint: isolated branch; renderer/capture audit; runtime hardware/API, memory, depth-format, and color-format diagnostics surfaced in the developer Graphics panel and bounded app log; a two-frame stable world-space triangle collector with a 16,384-triangle output cap; capture support for both native display lists and WSC material batches; and a developer probe that builds/caches a static primitive acceleration structure through Metal when the device reports ray-tracing support. The probe is opt-in and releases its AS, input buffer, and scratch buffer when stopped.

The static capture waits for the same geometry instance key to recur across consecutive frames. Instance keys include the display-list geometry hash and camera-separated object transform, so moving actors are excluded from the static set rather than forcing an AS rebuild every frame. Capture storage is limited to 16,384 triangles while the probe is active, and the AS/scratch allocation is bounded to 64 MiB each. The developer Graphics panel reports candidate triangles, dropped capture triangles, AS size, and build count. Disabling the probe defers capture reset and Metal resource release to the render thread to avoid racing the UIKit callback against scene collection or command encoding. This is an AS construction diagnostic, not an image effect.

Not implemented yet: Metal ray dispatch/intersection, raster shading integration, ray quality controls, and reflections/GI. Ray tracing remains visually off because there is not yet a functional shadow effect to enable. A successful capability query, geometry capture, AS build, or Apple compilation does not count as a working shadow.

Linux tests: `CC=clang tests/run_linux_checks.sh`; focused geometry test: `tests/run_gfx_rt_scene_test.sh`; ASan/UBSan: compile `tests/test_gfx_rt_scene.c` with `src/pc/gfx/gfx_rt_scene.c` and `-fsanitize=address,undefined`. The Metal Apple SDK validation workflow also runs the focused test. These validate capture stability, coordinate transforms, malformed bounds, and resource caps, not Metal intersection.

No Apple SDK build, A16/M1/M4 device run, RT intersection, visual comparison, frame-time comparison, or IPA is claimed for this checkpoint. The next implementation step is an independently testable Metal intersector using the built scene AS, followed by a real lighting/shading integration that does not darken HUD/UI content.
