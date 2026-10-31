#!/usr/bin/env python3
"""Source regression checks for the opt-in Metal corruption diagnostics."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    header = (ROOT / "src/pc/gfx/gfx_ios_diagnostics.h").read_text()
    uikit = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
    shader = (ROOT / "src/pc/gfx/gfx_metal_shader.c").read_text()
    metal = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
    metal_state = (ROOT / "src/pc/gfx/gfx_metal_state.c").read_text()
    metal_state_header = (ROOT / "src/pc/gfx/gfx_metal_state.h").read_text()
    pc = (ROOT / "src/pc/gfx/gfx_pc.c").read_text()
    stub = (ROOT / "tests/metal_backend_diagnostics_stub.m").read_text()

    for name, value in {
        "GFX_IOS_RENDER_NORMAL": 0,
        "GFX_IOS_RENDER_UNTEXTURED": 1,
        "GFX_IOS_RENDER_UV": 2,
        "GFX_IOS_RENDER_TEXTURE_ID": 3,
        "GFX_IOS_RENDER_TRIANGLES": 4,
    }.items():
        require(f"{name} = {value}" in header, f"missing or changed mode {name}")
    require("static int sm64RenderDiagnosticMode;" in uikit,
            "render diagnostics must default to Normal")
    require("@selector(cycleRenderDiagnosticMode)" in uikit,
            "render mode is not reachable from native diagnostic UI")
    require("sm64RenderDiagnosticMode = (sm64RenderDiagnosticMode + 1) % 5" in uikit,
            "render mode cycle must include every selectable mode")
    require("Render diagnostic mode changed" in uikit,
            "mode changes must be written to the persistent device log")
    require('gfx_ios_diag_log("Texture-cache diagnostics %s"' in uikit,
            "Tex log toggle must report enabled/disabled state")

    for token in (
        "diagnosticMode==1u?float4(1.0):sampleTex",
        "diagnosticMode==2u",
        "diagnosticMode==3u",
        "diagnosticMode==4u",
        "vertex_id [[vertex_id]]",
        "o.barycentric=",
    ):
        require(token in shader, f"MSL generator is missing diagnostic behavior: {token}")
    require("diagnosticTextureColor(params.textureId0)" in shader,
            "texture identity mode must use logical texture IDs")

    for field in ("filter0", "filter1", "diagnostic_mode", "texture_id0",
                  "texture_id1", "frame_count"):
        require(field in metal.split("struct MetalFragmentParams {", 1)[1].split("};", 1)[0],
                f"Metal fragment parameter is missing {field}")
    require("setFragmentBytes:&fragment_params length:sizeof(fragment_params) atIndex:1" in metal,
            "every shader variant must receive diagnostic parameters")
    require("gfx_ios_render_diagnostic_mode(void)" in stub and
            "gfx_ios_texture_cache_diagnostics_enabled(void)" in stub,
            "ROM-independent Apple link harness must stub the native UI diagnostic getters")
    for token in (
        "Metal render snapshot frame=",
        "Metal render texture frame=",
        "Metal render attribute frame=",
        "Metal texture upload sample sequence=",
        "samplePixels=%lu",
        "rgba0=(%u,%u,%u,%u)",
        "vertexBuffer=%p",
        "sampler=%p",
        "addressMode=(0x%x,0x%x)",
        "textureIdentityMatches=%d",
        "repeatedTextureImmediately=%d",
        "batch_id <= 12 || batch_id % 256 == 0",
        "snapshot_id <= 16 || snapshot_id % 128 == 0",
    ):
        require(token in metal, f"missing bounded Metal snapshot field: {token}")
    require("CAMetalLayer *metal_layer" in metal and "[metal_layer nextDrawable]" in metal,
            "diagnostic changes must preserve per-frame layer drawable acquisition")

    require("struct GfxMetalSamplerState" in metal_state_header and
            "gfx_metal_sampler_state_matches" in metal_state,
            "sampler binding must have a host-testable state comparison")
    select = metal.split("static void gfx_metal_select_texture", 1)[1].split(
        "static void gfx_metal_upload_texture", 1)[0]
    require("current_texture->sampler_state_valid" in select and
            "gfx_metal_apply_sampler_parameters(tile, desired.linear" in select,
            "texture selection must restore its sampler when the Metal slot differs")
    set_sampler = metal.split("static void gfx_metal_set_sampler_parameters", 1)[1].split(
        "static void gfx_metal_set_depth_test", 1)[0]
    for token in (
        "bound_textures[sampler]->sampler_state.linear = linear",
        "bound_textures[sampler]->sampler_state.cms = cms",
        "bound_textures[sampler]->sampler_state.cmt = cmt",
        "gfx_metal_apply_sampler_parameters(sampler, linear, cms, cmt)",
    ):
        require(token in set_sampler,
                f"sampler updates must be retained with the selected texture: {token}")
    shutdown = metal.split("static void gfx_metal_shutdown", 1)[1].split(
        "struct GfxRenderingAPI gfx_metal_api", 1)[0]
    require("sampler_state_valid[0] = sampler_state_valid[1] = false" in shutdown,
            "renderer shutdown must invalidate tile sampler state before a later initialization")

    require("gfx_texture_cache_high_water" in pc,
            "source renderer must record texture cache high-water mark")
    for token in (
        "gfx source render snapshot triangle=",
        "normalizedUV0=",
        "clip0=",
        "rgbInputMap=",
        "shade0=",
        "cacheOccupancy=",
        "cacheHighWater=",
        "gfx source texture snapshot triangle=",
        "snapshot_id <= 16 || snapshot_id % 256 == 0",
    ):
        require(token in pc, f"missing bounded CPU render snapshot field: {token}")

    print("Opt-in Metal render diagnostic source checks passed")


if __name__ == "__main__":
    main()
