#!/usr/bin/env python3
"""Source-level invariants for configurable Metal render targets and settings."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    state_h = (ROOT / "src/pc/gfx/gfx_metal_state.h").read_text()
    state_c = (ROOT / "src/pc/gfx/gfx_metal_state.c").read_text()
    state_test = (ROOT / "tests/metal_state_test.c").read_text()
    shader_h = (ROOT / "src/pc/gfx/gfx_metal_shader.h").read_text()
    shader_c = (ROOT / "src/pc/gfx/gfx_metal_shader.c").read_text()
    shader_emit = (ROOT / "tests/metal_shader_emit.c").read_text()
    metal_h = (ROOT / "src/pc/gfx/gfx_metal.h").read_text()
    metal = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
    uikit = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
    main_source = (ROOT / "src/pc/pc_main.m").read_text()

    for token in (
        "struct GfxMetalSettings", "struct GfxMetalCapabilities",
        "struct GfxMetalPerformanceStats", "gfx_metal_settings_sanitize",
        "gfx_metal_target_size", "gfx_metal_pipeline_cache_index",
    ):
        require(token in state_h, f"missing portable graphics settings API: {token}")
    for token in (
        "gfx_metal_classic_settings", "gfx_metal_enhanced_settings",
        "capabilities.sample_count_4", "capabilities.sample_count_2",
        "anisotropy_levels[] = { 1u, 2u, 4u, 8u, 16u }",
        "settings.render_scale_percent != 75u",
        "sample_index * 16u + render_state_key",
    ):
        require(token in state_c, f"missing settings normalization behavior: {token}")
    for token in (
        "gfx_metal_enhanced_settings()", "classic_only", "no_4x",
        "gfx_metal_target_size(2796, 1290, 75", "gfx_metal_pipeline_cache_index(4, 15) == 47",
    ):
        require(token in state_test, f"missing host regression case: {token}")

    require("gfx_metal_present_shader_source" in shader_h and
            "sm64_present_vertex" in shader_c and "sm64_present_fragment" in shader_c,
            "present pass shader must be shared with the Apple Metal compiler validation")
    require('"enhanced_present"' in shader_emit,
            "Apple validation shader emitter must emit the exact final-pass source")

    for token in (
        "supportsTextureSampleCount:2", "supportsTextureSampleCount:4",
        "MTLTextureType2DMultisample", "descriptor.sampleCount = sample_count",
        "MTLStoreActionMultisampleResolve", "resolveTexture = metal_resolve_texture",
        "gfx_metal_encode_final_pass", "metal_frame_uses_offscreen && !gfx_metal_encode_final_pass",
        "metal_offscreen_runtime_failed", "fallback_to_direct(\"enhanced pipeline or final-pass encoder failed\")",
        "[metal_layer nextDrawable]", "[metal_command presentDrawable:submitted_drawable]",
        "gfx_metal_pipeline_cache_index", "desc.sampleCount = metal_active_sample_count",
        "metal_frame_uses_offscreen ?\n        MTLPixelFormatRGBA16Float : metal_view.colorPixelFormat",
        "MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead",
        "metal_frame_uses_offscreen ?\n                MTLStoreActionStore : MTLStoreActionDontCare",
        "descriptor.maxAnisotropy = level", "MTLSamplerAddressModeMirrorRepeat",
        "SM64MetalPreset", "SM64MetalRenderScale", "SM64MetalSampleCount",
        "SM64MetalAnisotropy", "completed.GPUStartTime", "completed.GPUEndTime",
        "metal_performance_stats.draw_calls", "metal_performance_stats.triangles",
    ):
        require(token in metal, f"missing Metal backend capability or fallback invariant: {token}")
    require("gfx_metal_settings_use_offscreen(metal_settings)" in metal,
            "Classic 100%/1x render must stay on the direct drawable path")
    require("if (key != UINT32_MAX && metal_frame_uses_offscreen) key += 48u;" in metal and
            "#define METAL_PIPELINE_STATE_CAP 96" in metal,
            "BGRA8 and float-target pipelines need disjoint cache entries")
    require("void gfx_metal_apply_settings" in metal_h and
            "void gfx_metal_get_performance_stats" in metal_h,
            "settings and performance API must be visible to native UIKit")

    for token in (
        '[@"Classic", @"Enhanced"]', '[@"50%", @"75%", @"100%"]',
        '[@"Off", @"2×", @"4×"]', '[@"Off", @"2×", @"4×", @"8×", @"16×"]',
        "@selector(toggleGraphicsSettings)", "@selector(resetGraphicsSettings)",
        "@selector(applyGraphicsSettingsSelection:)", "sender == self.presetControl",
        "setEnabled:capabilities.sample_count_4", "GPU timing unavailable",
        "gfx_metal_classic_settings()",
    ):
        require(token in uikit, f"missing native graphics settings UI control: {token}")
    require("gfx_metal_set_cpu_frame_time_ms" in main_source,
            "CPU timing must cover the game tick separately from Metal GPU timing")

    print("Configurable Metal renderer foundation source checks passed")


if __name__ == "__main__":
    main()
