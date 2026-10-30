# RENDER96 texture pack compatibility

## Audit result

The repository's `gfx` tree has the same three top-level groups as the SM64 PC external texture layout: `actors`, `levels`, and `textures`. The current master tree is organized by actor and level names; sample assets use names such as `*.rgba16.png`. The pack README instructs Render96ex users to copy the `gfx` contents into the built game's `res/gfx` directory. This is a strong filename/layout match for this port's existing external texture loader.

The repo's public page identifies a GPL-3.0 license, but the README says the collection contains ESRGAN upscales, some textures Nintendo originally used, and contributor-created/source images. A repository-level license does not establish provenance or distribution rights for every image. Do not copy this pack into the source tree or IPA. Before redistribution, review the origin and permissions for each included asset and the requirements that apply to the collection.

## Existing loader fit and current gap

`FS_TEXTUREDIR` is `gfx`. With `EXTERNAL_DATA=1`, SM64's asset pipeline stores external texture names instead of their decoded game texture bytes. At runtime, `gfx_pc.c` constructs `gfx/<name>.png`, loads PNG data through stb_image as four-channel RGBA8, and sends it through `GfxRenderingAPI.upload_texture`. The preloader recognizes format suffixes before `.png`, including `rgba16`, matching the pack's standard PNG naming. Therefore, an externally supplied matching file can replace the named texture without changing shader-combiner code.

The current iOS build defaults to `EXTERNAL_DATA=0`, so it embeds/decodes the original game textures and never looks for these PNG paths. The existing `EXTERNAL_DATA` mode is a build-time choice; switching it on changes how extracted texture references are represented. Its preloader defaults to enabled and walks the whole `gfx` tree. That is not a safe iPhone policy for a multi-gigabyte pack. The current code also treats a missing external PNG as a missing-texture image rather than falling back to the original N64 texture. So this pack is **not usable as an optional enhancement in the current iOS executable**.

The later runtime design should add a filesystem override layer to the existing texture lookup: try an imported pack's `gfx/<canonical game texture name>.png`; if absent, use the original texture decode. Keep the default path entirely on original textures. Store user-imported packs in the app's Documents area through the iOS document picker, and let users enable or disable each pack. This requires renderer/application changes once, but pack selection and pack updates should not require another game executable build. Do not just enable `EXTERNAL_DATA` and point it at the entire pack: that loses the original-texture fallback and can eagerly allocate every replacement.

## Metal format, filtering, and memory

- The current Metal uploader creates `MTLPixelFormatRGBA8Unorm` textures from RGBA8 data. This is compatible with the PNG path after stb_image decoding. The DDS release variant is not supported by this loader and may use block-compressed formats requiring a separate iOS transcode path; start with PNG only.
- Textures currently use `mipmapped:NO`, no generated mip chain, and the backend has no mip LOD selection. The existing sampler states preserve N64 repeat/mirror/clamp and nearest/linear behavior. HD replacement should retain those wrap and base-level filtering semantics by default. Optional mipmapping can be added as an HD-only quality setting after measuring the visual change and cost; it should not silently change the normal renderer's N64 sampler behavior.
- At RGBA8, a `W × H` base level costs `4WH` bytes. A full mip chain is about `4WH × 4/3`: roughly 5.33 MiB for 1024² and 21.33 MiB for 2048², before alignment, CPU decode buffers, caches, render targets, and the game. These are per-texture estimates, not a pack total. The repository's Git size is not a runtime-memory estimate.
- The current external-data preloader defaults to loading every texture. The Metal backend retains each allocated `MTLTexture`, has a fixed 8192 texture-ID ceiling, and does not evict textures. HD support needs lazy decode/upload, an explicit memory budget, dimension checks, and an LRU or scene-aware eviction policy. Decode work should happen off the render-encoding path where practical; Metal resource creation and upload still need synchronization and residency accounting.

## Ray-traced materials

Treat a replacement as the same logical texture/material identity, independent of the Metal texture pointer. Scene capture should record a stable game texture key plus pack/version identity for debugging. The ray material should sample the same decoded texture asset and preserve alpha, wrap, and filtering semantics. Ray-specific mip selection or compressed formats can be layered on later without changing material identity.

## References

- [RENDER96-HD-TEXTURE-PACK](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK) and its [README](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK/blob/master/README.md)
- [Pack LICENSE](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK/blob/master/LICENSE)
- [Render96ex texture installation notes](https://github.com/Render96/Render96ex/blob/master/README.md)
