# SHADER64 Mission 81: architecture and API 2

## Current renderer map

```text
Fast3D / WSC command stream
  -> native CC combiner feature generation
  -> per-batch Metal vertex layout and native MTLRenderPipelineState
  -> offscreen RGBA16Float scene target (when effects or SHADER64 need it)
  -> built-in AO / bloom / god-rays / shadows / final composite, or SHADER64
  -> drawable
  -> native HUD and developer UI
```

`gfx_metal.c` is not a generic scene graph renderer: `gfx_metal_vertex_layout.h` describes the existing per-batch vertex data. The leading attribute is clip-space `float4`; optional fields include UV0/UV1, fog, and up to four combiner inputs. Normals, object identifiers, and a general world transform are not part of that draw interface. `gfx_scene_capture` separately records bounded world-space triangles with normals, UVs, colors, material/texture identifiers, camera matrices, and object transforms when it can split the model-view matrix. That capture currently feeds specific AO/shadow work; it is not yet a general programmable geometry submission path.

WSC materials are translated through the existing scene-material and Fast3D/combiner interface in `gfx_pc.c`; WSC geometry is not a separate renderer. Current WSC4/WSC5 content therefore continues to render via the native batch path, but custom packs cannot independently shade an imported material yet. API v2 adds no new WSC file requirements.

| Information | Existing engine path | API 1 / API 2 status |
|---|---|---|
| Final scene color | Resolved/offscreen Metal color texture | Exposed to both postprocess APIs |
| Depth, AO, built-in shadow output | Native final-composite bindings; depth is device depth | Not exposed to external packs yet |
| Camera and inverse projection | `gfx_scene_capture` frame metadata | Not exposed |
| World positions/normals/transforms | Bounded scene-capture triangles when valid | Not used by external geometry stages |
| Fast3D UV/color/combiner inputs | Per-batch vertex layout and generated shader | Native path only |
| WSC material category and textures | Existing scene-material flags and texture IDs | Not an external material binding contract |
| Motion vectors/history | No stable per-vertex motion stream | Unsupported |
| HUD | Separate native overlay after scene composite | Remains outside custom graph |

## API 2 vertical slice

API 2 (`formatVersion: 2`, `shaderApiVersion: 2`) is a Metal-native fullscreen raster DAG. It is intentionally a small first slice, not a claim of Iris/OptiFine compatibility. Each pass compiles its own Metal source with the common `shader64_pass` entry. The engine supplies a fullscreen triangle, one sampled input image, one clamp/linear sampler, and four pack parameters. Passes write either a named RGBA16Float sampled target or the drawable resource `present`.

The Fast3D/GeoLayout integration tags iOS Metal display-list items as static world, dynamic object, or Mario. An optional API 2 `materialShader` hook now runs after the existing N64 combiner and fog calculation, so packs can change the resulting color by material class while the Fast3D material remains the input and authority. Its stable entry point is `float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition, uint materialClass, constant float4 &params)`. Class IDs are 0 unclassified, 1 world, 2 object, and 3 character. UV is zero when the Fast3D draw has no texture coordinates; `screenPosition` is the Fast3D raster position, not a world-space position; `params` is the API 2 four-float parameter vector. Each draw receives the class through the fragment constant block. A selectable Material Class diagnostic visualizes the same classification.

The hook does **not** yet provide world-space positions, normals, transforms, independent material texture bindings, or engine depth. It is a post-combiner fragment customization point, not a programmable geometry/deferred renderer. Failed shader generation/compilation is logged and falls back to the original Fast3D combiner for that program, preserving gameplay/render recovery rather than presenting an unsupported shader as active success.

An API 2 pass may opt into the auxiliary `sceneDepth` resource with `"sceneDepth": true`. It then implements `shader64_pass_depth(color, uv, image, imageSampler, sceneDepth, depthSampler, inverseProjection, params)`. `sceneDepth` is the native Depth32Float hardware depth sampled over the scene viewport; its values are nonlinear Metal depth in [0,1], and 1 represents clear/background. `inverseProjection` is the current engine inverse projection matrix; screen UV maps x to [-1,1] and flips y to Metal clip coordinates, while depth is mapped from [0,1] to [-1,1] before unprojection. Scene depth is read-only and bound only for opted-in passes. Camera capture is enabled only while a graph requests it. If a valid single-sample depth texture or projection is unavailable, the renderer logs and uses its built-in final composite for that frame. Custom passes that need linear/view-space values must reconstruct them from this documented pair; no normals or world positions are implied.

An API 2 pass may instead request `"sceneNormals": true` and implement `shader64_pass_normals(color, uv, image, imageSampler, sceneNormals, normalSampler, params)`. The engine derives normals from neighboring samples of scene depth and inverse projection in a compute pass, then binds a half-resolution private RGBA16Float texture. RGB stores a view-space normal encoded from [-1,1] to [0,1]; alpha 1 marks a valid reconstruction and alpha 0 marks sky or a depth discontinuity. This is a screen-space estimate, not native vertex normals or a world-space G-buffer. It requires single-sample scene depth. The resource cannot share a pass with explicit scene-depth, lighting, or shadow bindings because those contracts currently occupy overlapping Metal slots. It can be combined with `packTextures`: the extended entry point is `shader64_pass_normals_textured` and keeps normal at texture 1 and pack images at slots 4–7. `NormalDebug64` and `NormalTexture64` demonstrate the separate and combined contracts. Apple MSL compilation and actual GPU output are pending; orientation and edge behavior still need device validation.

Reference diagnostic packs now use only these public resources: `DepthDebug64` shades raw nonlinear depth, `NormalDebug64` displays the engine's reusable depth-derived view-space normal resource, and `MaterialDebug64` colors the existing world/object/character class signal through the external material hook. The normal display is not a native per-vertex normal attachment and is expected to have discontinuity artifacts at silhouettes.

Passes may also request `"sceneLighting": true`. This implies scene depth and supplies the current inverse projection, inverse view, and `sunDirectionAndIntensity` (world-space direction in xyz, configured engine intensity in w). The pass must implement `shader64_pass_lit`; `Lumen64Graph` demonstrates per-pixel directional diffuse shading from a view normal reconstructed from depth. Its ambient floor is a pack parameter. This is a depth-derived screen-space lighting technique: it has silhouette/edge artifacts, no material-specific roughness or world position, and no custom shadow-map input. It is separate from the built-in engine shadow-map receiver and does not claim global illumination.

Passes may request `"sceneShadows": true` to consume the native directional shadow map. This implies scene depth and lighting and uses the `shader64_pass_shadowed` contract, with scene depth at texture 1, shadow depth at texture 2, inverse projection/view and sun inputs at buffers 1–3, and the fitted world-to-shadow basis/ranges at buffers 4–7. `Lumen64ShadowGraph` reconstructs position and normal from real scene depth and performs a 3×3 PCF comparison against the renderer's shadow map. That map uses the existing bounded scene-capture geometry path (up to 8192 source triangles). When the map, world-space caster set, or valid camera matrices are unavailable, that graph falls back to the built-in final composite for the frame. This is one existing native shadow map, not a general user-defined shadow-camera/pass system; coverage, filtering quality, and appearance still require physical-device validation.

Passes may request `"packTextures": true` and use `shader64_pass_textured`. The pack manifest declares an ordered `textures` array of at most four `{id, source}` objects; source paths must be direct `textures/*.png` paths. Bindings are `packTexture0..3` and their samplers at slots 4–7, `packTextureCount` at buffer 1, and normal API 2 params at buffer 0. Slots beyond count alias slot zero; count lets a shader avoid depending on aliases. This initial contract cannot be combined on one pass with scene depth, scene lighting, or scene shadows. The installer bounds each PNG to 4 MiB and the complete ZIP to 8 MiB; iOS decoding limits each dimension to 2048 and the pack to 4 megapixels before allocating its RGBA buffer. Images upload as premultiplied RGBA8, and active graph ownership keeps them alive through in-flight command buffers. Missing or invalid textures reject candidate activation and leave the prior graph active. `TextureDemo64` is a generated checker-pattern reference pack.

```json
{
  "formatVersion": 2,
  "id": "ExampleGraph",
  "name": "Example graph",
  "author": "Author",
  "version": "1.0",
  "shaderApiVersion": 2,
  "stage": "render_graph",
  "materialShader": {"source":"shaders/material.metal"},
  "parameters": [
    {"name":"characterTint", "label":"Character tint", "type":"float", "default":0.65, "min":0, "max":1}
  ],
  "passes": [
    {"id":"first", "type":"fullscreen", "input":"sceneColor", "output":"filtered",
     "source":"shaders/first.metal", "scale":0.5},
    {"id":"final", "type":"fullscreen", "input":"filtered", "output":"present",
     "source":"shaders/final.metal", "scale":1.0}
  ]
}
```

`materialShader` is optional. Its source file implements the hook above and is capped at the same 256 KiB source limit as graph passes. The material hook shares the graph pack's four parameters. GraphDemo uses its fourth parameter to tint only material class 3, leaving static world geometry on the original combiner output. A material may declare `"packTextures": true`; this enables the same manifest-ordered `textures` array for actual Fast3D material shading. The texture-aware contract passes `materialTexture0..3`, samplers, and `materialTextureCount` to `shader64_material`; absent slots alias slot zero, and count is authoritative. Texture resources share the existing four-image / four-megapixel bounds and graph-owned lifetime. Paper64MaterialGraph exercises a subtle paper-grain modulation on Mario and world materials. Host importer and Apple MSL compilation cover the interface; physical-device output remains unverified.

Each source declares `float4 shader64_pass(float4 color, float2 uv, texture2d<float> image, sampler imageSampler, constant float4 &params)`. The generic `color` argument is the current input sample. Passes can read nearby texels from `image`; they cannot access depth or other pass outputs except through the declared input edge.

Initial limits: four passes, three named intermediate targets, direct `shaders/*.metal` files, 256 KiB source per pass, full-resolution `present`, 0.25–1.0 scale for intermediate targets, and a 32 MiB RGBA16Float intermediate-target budget. The graph rejects duplicate IDs/resources, undeclared reads, feedback, cycles, unconsumed targets, unsupported node types, and invalid scales. The iOS runtime rechecks actual drawable dimensions against the byte budget. Candidate sources and pipelines compile before activation; failure preserves the currently active API 1/API 2 renderer. GPU programs remain untrusted and cannot be safely preempted by source validation.

Metal encoder ordering provides the pass sequence on one command buffer; render attachments use explicit render-target and shader-read usage. Textures are retained with the active graph until command-buffer completion. Metal's documented resource-ordering guarantees and completion handlers are used; no manual memory barrier is added for this serial render-encoder chain. Intermediate textures are not memoryless because later passes sample them. Apple documents render pass attachment/load/store behavior, resource synchronization, and runtime source compilation here: [render pass descriptor](https://developer.apple.com/documentation/metal/mtlrenderpassdescriptor), [resource synchronization](https://developer.apple.com/documentation/metal/resource-synchronization), and [runtime Metal source compilation](https://developer.apple.com/documentation/metal/mtldevice/makelibrary%28source%3Aoptions%3Aerror%3A).

## Bliss 2.1.2 workload audit

Inspected local archive: `Bliss_v2.1.2_(Chocapic13_Shaders_edit).zip`, SHA-256 `f41db92acc585fe9ffe0cc124d6ecaea27cef9ded91129ebc2a8e60226947b2c`. The ZIP contains 356 entries. It is a Minecraft/Iris shader pack with dimension-specific GLSL vertex/fragment programs, setup compute, shadow and shadow-composite stages, deferred stages, a sequence of composite stages, final presentation, shader libraries, block/entity metadata, and noise textures. Its `shaders.properties` configures multiple render targets, pass/blend behavior, optional Iris/Distant Horizons paths, and a large settings surface.

| Bliss capability/input | SHADER64 mapping | Current state |
|---|---|---|
| Composite/final image passes | API 2 fullscreen DAG; one image in/out per pass | Basic chained passes executable; no multi-attachment MRT |
| Final camera-gridline option | API 2 screen-space pass | Included as licensed adaptation pack; GUI hide flag is represented by a pack toggle |
| Engine scene depth and camera projection | Optional `sceneDepth` input on API 2 fullscreen nodes plus inverse projection | Implemented; DepthDebug64 shades raw values. Single-sample depth is required when queried. |
| Reusable scene-normal resource | Optional `sceneNormals` API 2 compute-derived, half-resolution view-space normal texture | Implemented in source and host validation; Apple MSL compilation and device output pending. Depth-derived approximation only; sky/discontinuities invalidated; no native vertex normals. |
| Configured directional sun and camera transforms | Optional `sceneLighting` resource on API 2 fullscreen nodes | Implemented for Lumen64Graph; depth-derived screen-space diffuse with edge artifacts |
| Native directional shadow depth and fitted world-to-shadow basis | Optional `sceneShadows` resource on API 2 fullscreen nodes | Implemented by reusing the bounded native map; Lumen64ShadowGraph includes 3×3 PCF; one map, up to 8192 captured source triangles, device appearance unverified |
| Pack-owned textures | Manifest-ordered PNGs bound to opted-in API 2 fullscreen passes or material hooks | Implemented for four PNGs maximum with bounded ZIP/decode/upload; TextureDemo64 and Paper64MaterialGraph exercise fullscreen and surface paths; GPU output remains device-unverified |
| Combined normals and pack textures | Scene-normal plus PNG bindings in one API 2 fullscreen pass | Implemented in source and host tests; NormalTexture64 reference pack added. Apple MSL validation pending. |
| Paper64 material-level toon style | Optional API 2 material hook on actual Fast3D draws | Paper64MaterialGraph quantizes combiner-color luminance and warms Mario independently; no normal-driven light bands/outlines, render appearance unverified |
| Material categories and post-combiner shading | Optional API 2 material hook with world/object/character category | Implemented; MaterialDebug64 exposes categories as actual surface colors. No normals, world position, or full albedo/roughness G-buffer |
| Setup compute and voxel lighting | Needs compute nodes, storage buffers, atomic/resource contracts | Not available |
| Minecraft time/weather/biome/entity/player uniforms | Needs generic world state API and per-game adapters | Not available; no Minecraft concepts in core API |
| Blue/noise textures and custom LUTs | Needs bounded texture resources and pack-relative binding contract | Not available |
| Water, clouds, volumetrics, TAA | Needs material/geometry hooks, depth, time, history and motion vectors | Not available |

Bliss uses GLSL features and Iris/OptiFine naming conventions; there is no direct source execution path through Metal. A reusable translator can be evaluated later as a front end for a restricted GLSL subset (preprocessor, type/system-function compatibility, sampler and stage lowering), but it cannot replace semantic adapters for Minecraft uniforms, geometry stages, shadow maps, compute, or G-buffers. API 2 remains authored in Metal; no GLSL translator is implemented in this increment.

The inspected Bliss license permits sharing modified shader versions only with its attribution, title, non-monetization, and redistribution conditions. The sample pack is a small independently expressed adaptation of the optional `CAMERA_GRIDLINES` behavior in Bliss's `dimensions/final.fsh`, titled `-Bliss Camera Gridlines Adaptation- (Chocapic13' Shaders edit)`, and includes the archive's license text and attribution. The archive itself and its other shader sources/assets are not copied into the repository. This pack is an effect-level compatibility proof, not a conversion of Bliss as a whole.

## Explicitly unsupported in this milestone

API 2 currently has bounded fullscreen raster nodes plus a post-combiner material hook. The engine directional shadow map is an optional sampled input, but user-defined shadow cameras/pass nodes are not supported. Pack PNGs are available to opted-in fullscreen and material passes, and can be combined with scene normals. They remain incompatible with explicit scene-depth/light/shadow bindings on one fullscreen pass. Geometry-stage overrides, user-defined compute graph nodes, deferred lighting, native per-pixel normal/albedo/material G-buffer attachments, history/ping-pong, pass dependencies beyond one sampled input, configurable load/store/stencil, resource aliasing/heaps, device-feature declarations/fallbacks, include files, and GLSL translation are unsupported and not advertised. API 1 source and `.metallib` loading remain available. API 2 graph nodes accept Metal source only; external `.metallib` graph nodes are not supported yet.
