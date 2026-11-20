# SHADER64 shader pack format (API 1)

SHADER64 is an optional user shader interface layered on the existing Metal renderer. The built-in Classic64 path remains the recovery/default path. The initial runtime contract supports one replaceable stage: **postprocess**, after the scene has been rasterized and before the native HUD is drawn. SM64 Fast3D material combiners, world geometry, shadow generation, and HUD rendering continue to use the engine's native code.

Mission 81 adds an incremental API 2 option: a bounded Metal fullscreen render graph with up to four source-compiled raster passes and named sampled intermediate images. API 1 remains supported unchanged. API 2 does not yet expose geometry/material, engine depth, shadow maps, G-buffers, compute, history, or custom texture bindings; see [`SHADER64_MISSION_81.md`](SHADER64_MISSION_81.md) for the current resource map, graph contract, Bliss workload audit, and limitations.

## Installing a pack

Create a ZIP whose root contains `manifest.json` and either `shaders/postprocess.metal` or `shaders/postprocess.metallib` (an optional explicit `shaders/` directory entry is allowed). In Graphics, tap **Shader Pack**, then **Import ZIP Pack…**, choose the archive in Files, and select the installed pack. Compilation/loading happens on selection. If compilation, library loading, or pipeline creation fails, the prior pipeline stays active and the error is shown. Choose **Classic64** to restore the original renderer immediately. The active pack and four parameter values persist across launches; a missing or failing pack automatically falls back to Classic64.

The v1 installer accepts ZIP stored or deflate entries, up to 8 MiB compressed/expanded total, 64 entries, 10 recognized files, 64 KiB manifest, 256 KiB per shader source, and 4 MiB per compiled library. It rejects traversal paths, absolute paths, backslashes, symlinks, encrypted entries, ZIP64, unknown files and unsupported compression. Textures, presets, nested `require`-style source includes, and multi-pass packs are not part of API 1.

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
    {"name":"strength", "label":"Strength", "type":"float", "default":1.0, "min":0.0, "max":1.0},
    {"name":"paper", "label":"Paper texture", "type":"boolean", "default":0},
    {"name":"grade", "label":"Color grade", "type":"select", "default":0,
     "options":["Neutral", "Warm", "Cool"]}
  ]
}
```

The ID is 1–64 ASCII letters, digits, underscore, or hyphen. At most four parameters are allowed. A `float` declares finite `min`, `default`, and `max` within `[-16, 16]` with `min <= default <= max`; the Graphics UI renders a bounded slider. A `boolean` uses default `0` or `1` and renders a toggle. A `select` uses an integer default index and 2–8 non-empty option labels (up to 48 characters each), and renders a segmented selector. In the shader these map to floats: the scalar value, 0/1, or selected option index. Values persist per pack, and the parameter UI can restore declared defaults. The pack list shows author, version, shader API, stage, and entry point. API version mismatches are rejected. `source` may be `shaders/postprocess.metal` or `shaders/postprocess.metallib`; both forms use the same API v1 postprocess contract.

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

### Precompiled iOS Metal libraries

For a `.metallib` pack, compile a source that exports `sm64_shader64_vertex` and `sm64_shader64_fragment` with the exact argument bindings shown in [`examples/compiled_postprocess.metal`](shaderpacks/examples/compiled_postprocess.metal). Keep the effect in `shader64_post`. With Xcode installed, build it for iPhoneOS:

```sh
xcrun --sdk iphoneos metal -c compiled_postprocess.metal -o compiled_postprocess.air
xcrun --sdk iphoneos metallib compiled_postprocess.air -o postprocess.metallib
```

Set `source` to `shaders/postprocess.metallib` and include the resulting library at that path. The runtime asks Metal to load the binary for the active device and creates a pipeline using the fixed API v1 entry points. Metal rejects incompatible libraries; a failed load leaves the active pipeline unchanged. The Apple validation workflow compiles and links this reference example. It does not execute it on a GPU, so runtime library loading and visual output still require a device run.

## Trust and compatibility

Metal source compilation is not a security sandbox. A pack can contain expensive or faulty GPU work and the runtime cannot reliably preempt every GPU workload on iOS. Install packs from sources you trust, keep the Classic64 recovery action available, and start with small bounded shaders. The app limits archive and source/library sizes, validates metadata and paths, and exposes only the scene texture and four float values, but it does not claim that arbitrary Metal source or libraries are memory-safe or immune to GPU stalls. API 1 supports runtime MSL compilation and Metal's compiled `.metallib` loading.

Version 1 accepts only a single postprocess replacement stage. Future APIs must be introduced with a versioned contract and explicit resources; packs requesting unavailable buffers, materials, passes, or device features must be rejected rather than silently approximated.

## Reference packs

- **Classic64** is the built-in original renderer and recovery path.
- **Paper64** demonstrates quantized color bands and image-neighbor outlines.
- **Lumen64 (Composite Demo)** demonstrates an exposure curve and saturation control. It is a screen-space composite example; it does not implement ray tracing, GI, or new world shadows.

The two source packs are in `ios/shaderpacks/`. Zip each pack directory's contents with the shown root layout to import it through Files.
