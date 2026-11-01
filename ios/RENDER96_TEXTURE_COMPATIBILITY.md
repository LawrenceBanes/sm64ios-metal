# RENDER96 texture pack compatibility

## Audit result and runtime support

The pack's `gfx` tree matches the SM64 PC external texture path: `actors`, `levels`, and `textures`, with filenames such as `actors/goomba/goomba_face.rgba16.png`. Five official-upstream sample images were checked against this checkout: Goomba face/body, Mario mustache, Bob-omb Battlefield texture `0`, and Castle interior texture `1`. Their canonical filenames match and their 512×512 RGBA PNGs preserve the 32×32 originals' aspect ratio.

The repository identifies itself as GPL-3.0, while the README says the collection mixes ESRGAN upscales, Nintendo source imagery, and contributor-created work. The repository-level license does not establish the provenance or distribution rights for every image. No pack artwork is copied into this repository or the IPA. The official release archive is about 392 MB compressed and expands to several gigabytes; users should obtain it from the [official release page](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK/releases/tag/1.3.26.7.7).

## Runtime integration

The iOS target now builds with `EXTERNAL_DATA=1`, which gives each texture a canonical asset-path identity and keeps the original PNGs in the app's `res/base.zip`. The renderer looks for a user replacement at `Documents/hd_textures/gfx/<canonical-name>.png`, then falls back to the bundled `gfx/<canonical-name>.png`. Its cache key uses the stable original path and preset variant, never a Metal texture ID or a cache-slot number.

Classic mode ignores the HD folder. Enhanced mode enables HD lookup by default. Missing, malformed, too-small, over-dimension, mismatched-aspect, or over-budget replacements fall back to the bundled original. The iOS build disables eager external-texture precaching: only textures reached by gameplay are read and uploaded. HD RGBA residency is capped at 256 MiB, each replacement is limited to 4096 pixels per side and a 64 MiB decoded base level, and unsafe relative paths are rejected.

The existing SM64 tile-based UV transform and Metal sampler state remain unchanged. Coordinates are normalized to the original tile dimensions; the inspected pack samples retain their aspect ratio, so that mapping continues over the larger image. PNG alpha is decoded to RGBA8. Repeat, mirror, clamp, point, and linear filtering keep their existing renderer behavior. DDS is not supported by this loader.

### Install and test

1. Download the official RENDER96 release from the [releases page](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK/releases/tag/1.3.26.7.7) and extract it on a computer with 7z support.
2. Copy the extracted pack's `gfx` folder into the app's Documents folder. The final examples should be `Documents/hd_textures/gfx/actors/goomba/goomba_face.rgba16.png` and `Documents/hd_textures/gfx/levels/bob/0.rgba16.png`. Create `hd_textures` if needed. Documents is available in Files because app file sharing is enabled.
3. Fully quit and relaunch the game after copying. Open the native Graphics panel and select **Enhanced**. Check Mario on the title screen, a Goomba, Peach's Castle, and Bob-omb Battlefield. To return to the original look, select **Classic**.

The loader also supports installing only selected `actors`, `levels`, or `textures` subfolders. The app does not bundle or redistribute the pack. Textures not supplied by the user remain original automatically.

## Metal format and memory

- The Metal uploader creates `MTLPixelFormatRGBA8Unorm` from stb_image RGBA8 output.
- Texture uploads currently have no mip chain. Existing N64 wrap and point/linear filtering remain intact; mipmap generation is a separate future improvement and is not claimed here.
- A 512×512 RGBA8 base level costs 1 MiB, and 1024×1024 costs 4 MiB, before decode buffers and alignment. The loader lazily keeps at most 256 MiB of HD base levels resident. Once full, it selects the bundled original. The renderer currently retains texture IDs and does not evict them; device evidence should guide any later scene-aware eviction.

## Ray-traced material identity

Scene materials should keep the canonical original texture path as their stable identity, with replacement pack/version metadata available for diagnostics. Future ray paths must preserve alpha, addressing, and filter behavior while keeping pack-specific mip selection separate from the original material identity.

## References

- [RENDER96 repository](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK), [README](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK/blob/master/README.md), [LICENSE](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK/blob/master/LICENSE)
- [Official release 1.3.26.7.7](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK/releases/tag/1.3.26.7.7)
- [Render96ex installation notes](https://github.com/Render96/Render96ex/blob/master/README.md)
