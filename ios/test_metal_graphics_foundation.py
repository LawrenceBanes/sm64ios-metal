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
    shadow_h = (ROOT / "src/pc/gfx/gfx_shadow_map.h").read_text()
    shadow_c = (ROOT / "src/pc/gfx/gfx_shadow_map.c").read_text()
    shadow_test = (ROOT / "tests/shadow_map_test.c").read_text()
    linux_runner = (ROOT / "tests/run_linux_checks.sh").read_text()
    shader_h = (ROOT / "src/pc/gfx/gfx_metal_shader.h").read_text()
    shader_c = (ROOT / "src/pc/gfx/gfx_metal_shader.c").read_text()
    shader_emit = (ROOT / "tests/metal_shader_emit.c").read_text()
    metal_h = (ROOT / "src/pc/gfx/gfx_metal.h").read_text()
    metal = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
    uikit = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
    gfx_pc = (ROOT / "src/pc/gfx/gfx_pc.c").read_text()
    rendering_api = (ROOT / "src/pc/gfx/gfx_rendering_api.h").read_text()
    render_tags = (ROOT / "src/pc/gfx/gfx_render_section.h").read_text()
    area = (ROOT / "src/game/area.c").read_text()
    main_source = (ROOT / "src/pc/pc_main.m").read_text()

    for token in (
        "struct GfxMetalSettings", "struct GfxMetalCapabilities",
        "struct GfxMetalPerformanceStats", "gfx_metal_settings_sanitize",
        "gfx_metal_target_size", "gfx_metal_pipeline_cache_index",
        "estimated_target_bytes", "bloom_pixels",
    ):
        require(token in state_h, f"missing portable graphics settings API: {token}")
    for token in (
        "gfx_metal_classic_settings", "gfx_metal_enhanced_settings",
        "gfx_metal_cinematic_settings", "gfx_metal_performance_settings", "gfx_metal_preset_name",
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
    for token in (
        "gfx_shadow_map_transform_point", "gfx_shadow_map_fit", "gfx_shadow_map_project", "center_x = roundf",
        "uv_depth[c] = c == 1 ? 1.0f - n : n",
    ):
        require(token in shadow_h + shadow_c, f"missing directional shadow math: {token}")
    require("Directional shadow fit" in shadow_test and "run_shadow_map_test.sh" in linux_runner,
            "directional shadow fit must have a host regression test in the Linux suite")

    require("gfx_metal_present_shader_source" in shader_h and
            "sm64_present_vertex" in shader_c and "sm64_present_fragment" in shader_c,
            "present pass shader must be shared with the Apple Metal compiler validation")
    require('"enhanced_present"' in shader_emit,
            "Apple validation shader emitter must emit the exact final-pass source")
    require('"ssao_compute"' in shader_emit and "gfx_metal_ssao_shader_source" in shader_emit,
            "Apple validation must compile the production SSAO compute source")
    require('"bloom_compute"' in shader_emit and "gfx_metal_bloom_shader_source" in shader_emit,
            "Apple validation must compile the production bloom compute source")
    require('"shadow_depth"' in shader_emit and "gfx_metal_shadow_shader_source" in shader_emit,
            "Apple validation must compile the production shadow-depth vertex source")
    require('"god_rays"' in shader_emit and "gfx_metal_god_ray_shader_source" in shader_emit,
            "Apple validation must compile the production screen-space light-shaft kernel")
    require("gfx_metal_encode_shadows(metal_command)" in metal and
            "setFragmentTexture:(metal_shadow_texture ?: metal_depth_texture) atIndex:4" in metal and
            "setFragmentBytes:&shadow_ranges length:sizeof(shadow_ranges) atIndex:11" in metal,
            "directional depth pass must feed the production final-pass receiver bindings")
    require("shadowMap.sample(shadowSampler" in shader_c and "shadowRanges.w>0.0" in shader_c,
            "receiver shader must sample the depth map with bounded PCF only when a valid fit exists")
    require("kernel void sm64_god_rays" in shader_c and "depthTex.read(p)>=0.999999" in shader_c and
            "gfx_postprocess_project_direction" in metal,
            "light shafts must use depth occlusion and project the directional source through the camera")
    require("smoothstep(0.014,0.021,sd)" in shader_c and "sky_params.y = 1.0f" in metal,
            "day/sunset sky modes must draw a depth-masked, camera-projected sun disk")
    require("gfx_metal_get_sun_direction" in metal and "gfx_metal_cycle_sun_preset" in uikit and
            "Change sun direction" in uikit and "sun_ray[axis] = -sun_ray[axis]" in metal,
            "sun direction presets must drive both projected sky/light shafts and the directional shadow pass")
    require("gfx_metal_sun_intensity" in metal and "shadowVisibility" in shader_c and
            "max(dot(worldNormal,normalize(sunParams.xyz)),0.0)" in shader_c and
            "sunParams.w>0.0" in shader_c and "inverse_projection_valid" in metal,
            "sun intensity must apply a depth-reconstructed world-normal diffuse contribution")
    require("openShadowSettings" in uikit and "gfx_metal_cycle_shadow_resolution" in uikit,
            "developer UI must expose a shadow toggle and resolution control")
    require("openGodRaySettings" in uikit and "gfx_metal_cycle_god_ray_quality" in uikit and
            "quarter-resolution screen-space approximation" in uikit,
            "developer UI must expose and correctly label the screen-space light-shaft approximation")
    require("CGFloat rowWidth = inner - gap * 4.0" in uikit and "UIButton *row1[]" in uikit and
            "UIButton *row3[]" in uikit and "settingsWidth - 28.0" in uikit,
            "graphics controls must reflow within the available iPhone panel width")
    require("enhanced effects suspended (direct path)" in uikit and
            "gfx_metal_offscreen_fallback_active()" in uikit,
            "developer performance panel must disclose enhanced-path fallback")

    for token in (
        "supportsTextureSampleCount:2", "supportsTextureSampleCount:4",
        "MTLTextureType2DMultisample", "descriptor.sampleCount = sample_count",
        "MTLStoreActionMultisampleResolve", "resolveTexture = metal_resolve_texture",
        "gfx_metal_encode_final_pass", "metal_frame_uses_offscreen && !gfx_metal_encode_final_pass",
        "if (metal_offscreen_runtime_failed) return false;",
        "gfx_metal_apply_settings(fallback);\n    @synchronized ([NSObject class]) { metal_offscreen_runtime_failed = true; }",
        "if (metal_frame_uses_offscreen && !gfx_metal_encode_final_pass(metal_drawable, metal_command)) {\n        metal_offscreen_runtime_failed = true;",
        "[metal_layer nextDrawable]", "[metal_command presentDrawable:submitted_drawable]",
        "gfx_metal_pipeline_cache_index", "desc.sampleCount = metal_active_sample_count",
        "metal_frame_uses_offscreen ?\n        MTLPixelFormatRGBA16Float : metal_view.colorPixelFormat",
        "MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead",
        "metal_frame_uses_offscreen ?\n                MTLStoreActionStore : MTLStoreActionDontCare",
        "descriptor.maxAnisotropy = level", "MTLSamplerAddressModeMirrorRepeat",
        "SM64MetalPreset", "SM64MetalRenderScale", "SM64MetalSampleCount",
        "SM64MetalAnisotropy", "completed.GPUStartTime", "completed.GPUEndTime",
        "metal_performance_stats.draw_calls", "metal_performance_stats.triangles",
        "gfx_metal_ssao_enabled", "gfx_metal_set_ssao_enabled", "gfx_metal_cycle_ssao_mode",
        "gfx_metal_set_post_enabled", "gfx_metal_set_fxaa_enabled", "gfx_metal_step_exposure",
        "gfx_metal_effects_enabled", "gfx_metal_set_effects_enabled",
        "gfx_metal_offscreen_fallback_active",
        "SM64MetalEffectsEnabled", "metal_performance_stats.ssao_pixels = 0",
        "gfx_metal_set_fog_enabled", "gfx_metal_set_bloom_enabled",
        "gfx_metal_set_ssao_quality", "gfx_metal_cycle_ssao_quality",
        "newComputePipelineStateWithFunction", "dispatchThreads:MTLSizeMake(width, height, 1)",
        "MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite",
        "sm64_ssao_denoise", "metal_ssao_filtered_texture",
        "sm64_bloom_extract", "sm64_bloom_blur", "gfx_metal_encode_bloom",
        "MTLPixelFormatRGBA16Float", "metal_bloom_output_texture",
    ):
        require(token in metal, f"missing Metal backend capability or fallback invariant: {token}")
    require("gfx_metal_settings_use_offscreen(metal_settings)" in metal,
            "Classic 100%/1x render must stay on the direct drawable path")
    require("GFX_RENDER_UI_OVERLAY_BEGIN_TAG" in render_tags and
            area.index("geo_process_root(gCurrentArea->unk04") < area.index("GFX_RENDER_UI_OVERLAY_BEGIN_TAG") < area.index("render_hud();"),
            "iOS Metal game output must split the 3D scene before HUD commands")
    require("begin_ui_overlay" in rendering_api and
            "gfx_flush();\n                    gfx_rapi->begin_ui_overlay();" in gfx_pc,
            "display-list traversal must flush the world batch before the optional overlay boundary")
    require("gfx_metal_encode_scene_composite();" in metal and
            "metal_scene_postprocessed_before_ui" in metal and
            "pass.colorAttachments[0].loadAction = composited ? MTLLoadActionLoad : MTLLoadActionClear;" in metal and
            "metal_frame_uses_offscreen = false;" in metal,
            "Metal must composite the world before rendering HUD commands to the drawable")
    require("if (key != UINT32_MAX && metal_frame_uses_offscreen) key += 48u;" in metal and
            "#define METAL_PIPELINE_STATE_CAP 96" in metal,
            "BGRA8 and float-target pipelines need disjoint cache entries")
    require("void gfx_metal_apply_settings" in metal_h and
            "void gfx_metal_get_performance_stats" in metal_h,
            "settings and performance API must be visible to native UIKit")

    for token in (
        '[@"Classic", @"Enhanced", @"Cinematic", @"Performance"]', '[@"50%", @"75%", @"100%"]',
        '[@"Off", @"2×", @"4×"]', '[@"Off", @"2×", @"4×", @"8×", @"16×"]',
        "@selector(toggleGraphicsSettings)", "@selector(resetGraphicsSettings)",
        "@selector(applyGraphicsSettingsSelection:)", "sender == self.presetControl",
        "setEnabled:capabilities.sample_count_4", "GPU timing unavailable",
        "gfx_metal_classic_settings()", "togglePostprocessing", "toggleFXAA", "stepExposure", "toggleFog", "toggleBloom",
        "toggleMasterEffects", "Effects: On", "targets ~%.1f MiB", "bloom_pixels",
    ):
        require(token in uikit, f"missing native graphics settings UI control: {token}")
    require("gfx_metal_set_cpu_frame_time_ms" in main_source,
            "CPU timing must cover the game tick separately from Metal GPU timing")
    for token in (
        "SM64MetalColorGrade", "gfx_metal_cycle_color_grade",
        "gfx_metal_set_color_grade", "grade_style", "Grade: %@",
        "gradeStyle [[buffer(5)]]", "gradeStyle.x", "gradeStyle.y",
    ):
        require(token in metal + metal_h + shader_c + uikit,
                f"missing persisted color-grading path: {token}")
    for token in (
        "SM64MetalFogProfile", "fog_density[]", "fog_start[]", "fog_colors[]",
        "gfx_metal_cycle_fog_profile", "gfx_metal_set_fog_profile", "Fog range: %@",
    ):
        require(token in metal + metal_h + uikit,
                f"missing bounded fog profile setting: {token}")
    for token in (
        "SM64MetalSSAOQuality", "quality_parameters[]", "quality_samples[]",
        "constant uint &sampleCount [[buffer(2)]]", "min(sampleCount,8u)",
        "AO quality: %@", "cycleAOQuality",
    ):
        require(token in metal + metal_h + shader_c + uikit,
                f"missing bounded SSAO quality control: {token}")
    for token in (
        "SM64MetalSkyPreset", "gfx_metal_cycle_sky_preset", "gfx_metal_set_sky_preset",
        "skyParams [[buffer(6)]]", "sceneDepth>=0.999999", "Sky: %@",
    ):
        require(token in metal + metal_h + shader_c + uikit,
                f"missing depth-masked sky preset: {token}")

    print("Configurable Metal renderer foundation source checks passed")


if __name__ == "__main__":
    main()
