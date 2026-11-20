# SHADER64 shader pack format (API 1)

SHADER64 is an optional user shader interface layered on the existing Metal renderer. The built-in Classic64 path remains the recovery/default path. The initial runtime contract supports one replaceable stage: **postprocess**, after the scene has been rasterized and before the native HUD is drawn. SM64 Fast3D material combiners, world geometry, shadow generation, and HUD rendering continue to use the engine's native code.

Mission 81 adds an incremental API 2 option: a bounded Metal fullscreen render graph with up to four source-compiled raster passes and named sampled intermediate images. API 1 remains supported unchanged. API 2 can optionally expose a post-combiner material hook, scene depth, camera/light transforms, the existing native directional shadow map, and up to four pack-owned PNG textures. Geometry-stage control, per-vertex normals/world positions, general G-buffers, compute, and temporal history remain unavailable; see [`SHADER64_MISSION_81.md`](SHADER64_MISSION_81.md) for the resource map, graph contracts, Bliss workload, and limitations.

## Installing a pack

Create a ZIP whose root contains `manifest.json` and either `shaders/postprocess.metal` or `shaders/postprocess.metallib` (an optional explicit `shaders/` directory entry is allowed). In Graphics, tap **Shader Pack**, then **Import ZIP Pack…**, choose the archive in Files, and select the installed pack. Compilation/loading happens on selection. If compilation, library loading, or pipeline creation fails, the prior pipeline stays active and the error is shown. Choose **Classic64** to restore the original renderer immediately. The active pack and four parameter values persist across launches; a missing or failing pack automatically falls back to Classic64.

The installer accepts ZIP stored or deflate entries, up to 8 MiB compressed/expanded total, 64 ZIP entries, 16 recognized files, a 64 KiB manifest, 256 KiB per shader source, 4 MiB per compiled library, and 4 MiB per PNG. It rejects traversal paths, absolute paths, backslashes, symlinks, encrypted entries, ZIP64, unknown files and unsupported compression. API 1 does not consume textures; API 2 can declare up to four direct `textures/*.png` resources. Runtime decoding is limited to 2048 pixels per dimension and 4 megapixels total per pack.

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

## API 2 material and scene depth resources

API 2 packs may add `"materialShader":{"source":"shaders/material.metal"}`. The function contract is:

```metal
float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition,
                         uint materialClass, constant float4 &params);
```

`materialClass` is 0 unclassified, 1 world, 2 object, or 3 character. The hook executes after Fast3D's combiner/fog output; it does not replace native geometry/material processing. `uv` is zero for an untextured draw. `screenPosition` is raster position, not world position.

An individual API 2 pass can declare `"sceneDepth":true` and implement the `shader64_pass_depth` contract documented in [`SHADER64_MISSION_81.md`](SHADER64_MISSION_81.md). Raw nonlinear Depth32Float samples and inverse projection are supplied only to those passes. Depth-dependent packs gracefully use the native composite on frames where the renderer cannot provide a single-sample depth texture and valid projection.

Reference packs include **DepthDebug64** (raw depth), **NormalDebug64** (screen-space normals reconstructed from neighboring depth samples), and **MaterialDebug64** (world/object/character class colors). Reconstructed normals are an approximation derived from depth discontinuities; the engine does not yet export a native normal attachment.

**Paper64MaterialGraph** demonstrates material-hook band quantization from the existing Fast3D combiner result and applies a separate warm adjustment to Mario's classified draws. It does not receive surface normals, so its bands are based on input color luminance and do not represent directional toon lighting or geometry outlines.

An API 2 pass can request `"sceneLighting":true` for scene depth, inverse projection/view, and the configured world-space sun direction/intensity. `Lumen64Graph` demonstrates depth-derived directional diffuse lighting. It is a screen-space approximation and does not include material-specific BRDFs or GI. `Lumen64ShadowGraph` can additionally request `"sceneShadows":true` to sample the existing engine directional shadow map. See the versioned resource contract in [`SHADER64_MISSION_81.md`](SHADER64_MISSION_81.md); runtime GPU appearance is not yet device-verified.

An API 2 pass may instead request `"sceneShadows":true` to receive the engine's existing bounded directional shadow map plus scene depth, camera transforms, fitted shadow basis, and sun data. See `Lumen64ShadowGraph` and the full binding contract in [`SHADER64_MISSION_81.md`](SHADER64_MISSION_81.md). It uses reconstructed screen-space normals and a single native shadow map; geometry coverage is bounded, and physical-device appearance remains unverified.

An API 2 manifest may declare up to four textures using `{ "id": "noise", "source": "textures/noise.png" }`. A fullscreen pass opts in with `"packTextures":true` and implements `shader64_pass_textured`, receiving `packTexture0..3`, their samplers, and `packTextureCount`; declaration order defines slots, and unused slots alias slot zero. This initial texture pass cannot also request scene depth, lighting, or shadows, though separate passes can still be chained. PNGs decode to premultiplied RGBA8 and are owned by the active graph until its in-flight GPU work completes. `TextureDemo64` demonstrates a user texture; runtime pixels have not been physically verified.

## Reference packs

- **Classic64** is the built-in original renderer and recovery path.
- **Paper64** demonstrates quantized color bands and image-neighbor outlines.
- **Lumen64 (Composite Demo)** demonstrates an exposure curve and saturation control. It is a screen-space composite example; it does not implement ray tracing, GI, or new world shadows.

The two source packs are in `ios/shaderpacks/`. Zip each pack directory's contents with the shown root layout to import it through Files.
