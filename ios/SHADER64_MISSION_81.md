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

`materialShader` is optional. Its source file implements the hook above and is capped at the same 256 KiB source limit as graph passes. The material hook shares the graph pack's four parameters. GraphDemo uses its fourth parameter to tint only material class 3, leaving static world geometry on the original combiner output. Host import and Apple MSL compilation cover the contract; the rendered Mario/world separation still needs physical-device visual confirmation.

Each source declares `float4 shader64_pass(float4 color, float2 uv, texture2d<float> image, sampler imageSampler, constant float4 &params)`. The generic `color` argument is the current input sample. Passes can read nearby texels from `image`; they cannot access depth or other pass outputs except through the declared input edge.

Initial limits: four passes, three named intermediate targets, direct `shaders/*.metal` files, 256 KiB source per pass, full-resolution `present`, 0.25–1.0 scale for intermediate targets, and a 32 MiB RGBA16Float intermediate-target budget. The graph rejects duplicate IDs/resources, undeclared reads, feedback, cycles, unconsumed targets, unsupported node types, and invalid scales. The iOS runtime rechecks actual drawable dimensions against the byte budget. Candidate sources and pipelines compile before activation; failure preserves the currently active API 1/API 2 renderer. GPU programs remain untrusted and cannot be safely preempted by source validation.

Metal encoder ordering provides the pass sequence on one command buffer; render attachments use explicit render-target and shader-read usage. Textures are retained with the active graph until command-buffer completion. Metal's documented resource-ordering guarantees and completion handlers are used; no manual memory barrier is added for this serial render-encoder chain. Intermediate textures are not memoryless because later passes sample them. Apple documents render pass attachment/load/store behavior, resource synchronization, and runtime source compilation here: [render pass descriptor](https://developer.apple.com/documentation/metal/mtlrenderpassdescriptor), [resource synchronization](https://developer.apple.com/documentation/metal/resource-synchronization), and [runtime Metal source compilation](https://developer.apple.com/documentation/metal/mtldevice/makelibrary%28source%3Aoptions%3Aerror%3A).

## Bliss 2.1.2 workload audit

Inspected local archive: `Bliss_v2.1.2_(Chocapic13_Shaders_edit).zip`, SHA-256 `f41db92acc585fe9ffe0cc124d6ecaea27cef9ded91129ebc2a8e60226947b2c`. The ZIP contains 356 entries. It is a Minecraft/Iris shader pack with dimension-specific GLSL vertex/fragment programs, setup compute, shadow and shadow-composite stages, deferred stages, a sequence of composite stages, final presentation, shader libraries, block/entity metadata, and noise textures. Its `shaders.properties` configures multiple render targets, pass/blend behavior, optional Iris/Distant Horizons paths, and a large settings surface.

| Bliss capability/input | SHADER64 mapping | Current state |
|---|---|---|
| Composite/final image passes | API 2 fullscreen DAG; one image in/out per pass | Basic chained passes executable; no multi-attachment MRT |
| Final camera-gridline option | API 2 screen-space pass | Included as licensed adaptation pack; GUI hide flag is represented by a pack toggle |
| Depth-aware motion blur, AO, fog, SSR | Needs named depth/history/depth-reconstruction resources | Not available to API 2 yet |
| Shadow map and shadow color textures | Needs declared depth/comparison textures and shadow pass nodes | Not available to user graphs; built-in engine shadow pass remains private |
| Deferred/G-buffer surfaces | Needs material/normal/albedo attachments and geometry hook | Not available |
| Setup compute and voxel lighting | Needs compute nodes, storage buffers, atomic/resource contracts | Not available |
| Minecraft time/weather/biome/entity/player uniforms | Needs generic world state API and per-game adapters | Not available; no Minecraft concepts in core API |
| Blue/noise textures and custom LUTs | Needs bounded texture resources and pack-relative binding contract | Not available |
| Water, clouds, volumetrics, TAA | Needs material/geometry hooks, depth, time, history and motion vectors | Not available |

Bliss uses GLSL features and Iris/OptiFine naming conventions; there is no direct source execution path through Metal. A reusable translator can be evaluated later as a front end for a restricted GLSL subset (preprocessor, type/system-function compatibility, sampler and stage lowering), but it cannot replace semantic adapters for Minecraft uniforms, geometry stages, shadow maps, compute, or G-buffers. API 2 remains authored in Metal; no GLSL translator is implemented in this increment.

The inspected Bliss license permits sharing modified shader versions only with its attribution, title, non-monetization, and redistribution conditions. The sample pack is a small independently expressed adaptation of the optional `CAMERA_GRIDLINES` behavior in Bliss's `dimensions/final.fsh`, titled `-Bliss Camera Gridlines Adaptation- (Chocapic13' Shaders edit)`, and includes the archive's license text and attribution. The archive itself and its other shader sources/assets are not copied into the repository. This pack is an effect-level compatibility proof, not a conversion of Bliss as a whole.

## Explicitly unsupported in this milestone

API 2 currently has fullscreen raster nodes only. Geometry stages, shadow graph nodes, compute, deferred lighting, named engine depth/normals/albedo/material buffers, custom texture resources, history/ping-pong, pass dependencies beyond one sampled input, configurable load/store/stencil, resource aliasing/heaps, device-feature declarations/fallbacks, include files, and GLSL translation are unsupported and rejected/not advertised. API 1 source and `.metallib` loading remain available as before. The API 2 graph accepts Metal source files; external `.metallib` graph nodes are not supported yet.
