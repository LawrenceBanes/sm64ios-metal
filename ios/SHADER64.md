# SHADER64 shader pack format (API 1)

SHADER64 is an optional user shader interface layered on the existing Metal renderer. The built-in Classic64 path remains the recovery/default path. The initial runtime contract supports one replaceable stage: **postprocess**, after the scene has been rasterized and before the native HUD is drawn. SM64 Fast3D material combiners, world geometry, shadow generation, and HUD rendering continue to use the engine's native code.

## Installing a pack

Create a ZIP whose root contains exactly `manifest.json` and `shaders/postprocess.metal` (an optional explicit `shaders/` directory entry is allowed). In Graphics, tap **Shader Pack**, then **Import ZIP Pack…**, choose the archive in Files, and select the installed pack. Compilation happens on selection. If compilation or pipeline creation fails, the prior pipeline stays active and the error is shown. Choose **Classic64** to restore the original renderer immediately. The active pack and four parameter values persist across launches; a missing or failing pack automatically falls back to Classic64.

The v1 installer accepts ZIP stored or deflate entries, up to 8 MiB compressed/expanded total, 64 entries, 10 recognized files, 64 KiB manifest and 256 KiB per shader source. It rejects traversal paths, absolute paths, backslashes, symlinks, encrypted entries, ZIP64, unknown files and unsupported compression. Textures, presets, nested `require`-style source includes and metallib-only packs are not part of API 1 yet.

## Manifest

```json
{
  "formatVersion": 1,
  "id": "Paper64",
  "name": "Paper64",
  "author": "Pack author",
  "version": "1.0.0",
  "shaderApiVersion": 1,
  "stage": "postprocess",
  "source": "shaders/postprocess.metal",
  "entryPoint": "shader64_post",
  "parameters": [
    {"name":"strength", "label":"Strength", "type":"float", "default":1.0, "min":0.0, "max":1.0}
  ]
}
```

The ID is 1–64 ASCII letters, digits, underscore, or hyphen. At most four float parameters are allowed. Defaults and bounds must be finite, `min <= default <= max`. UI parameter entry is clamped to each declared range. API version mismatches are rejected.

## Metal entry point

```metal
#include <metal_stdlib>
using namespace metal;

float4 shader64_post(float4 color, float2 uv,
                     texture2d<float> image, sampler imageSampler,
                     constant float4 &params) {
    return color;
}
```

`color` is the rasterized scene sample; `uv` is normalized screen space, `image` and `imageSampler` allow bounded-resolution neighbor samples, and `params[0..3]` contain the manifest parameter values. The stage outputs SDR color for the drawable. Scene depth, normals, object transforms, custom textures, and previous-frame data are not exposed in API 1. Custom packs replace the built-in final composite while active; consequently built-in AO, tone mapping, fog, bloom, FXAA, sun/shadow receiver, and light-shaft composition are bypassed. The built-in geometry rasterizer and post-composite HUD remain active.

## Trust and compatibility

Metal source compilation is not a security sandbox. A pack can contain expensive or faulty GPU work and the runtime cannot reliably preempt every GPU workload on iOS. Install packs from sources you trust, keep the Classic64 recovery action available, and start with small bounded shaders. The app limits archive and source sizes, validates metadata and paths, and exposes only the scene texture and four float values, but it does not claim that arbitrary Metal source is memory-safe or immune to GPU stalls. This initial release uses runtime MSL source compilation; precompiled `.metallib` loading is not enabled.

Version 1 accepts only a single postprocess replacement stage. Future APIs must be introduced with a versioned contract and explicit resources; packs requesting unavailable buffers, materials, passes, or device features must be rejected rather than silently approximated.

## Reference packs

- **Classic64** is the built-in original renderer and recovery path.
- **Paper64** demonstrates quantized color bands and image-neighbor outlines.
- **Lumen64 (Composite Demo)** demonstrates an exposure curve and saturation control. It is a screen-space composite example; it does not implement ray tracing, GI, or new world shadows.

The two source packs are in `ios/shaderpacks/`. Zip each pack directory's contents with the shown root layout to import it through Files.
