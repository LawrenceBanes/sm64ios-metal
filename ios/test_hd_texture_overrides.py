#!/usr/bin/env python3
"""Source checks for lazy iOS RENDER96 overrides and original-texture fallback."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    graphics = (ROOT / "src/pc/gfx/gfx_pc.c").read_text()
    override = (ROOT / "src/pc/gfx/gfx_texture_override.c").read_text()
    fs_h = (ROOT / "src/pc/fs/fs.h").read_text()
    fs_c = (ROOT / "src/pc/fs/fs.c").read_text()
    metal_h = (ROOT / "src/pc/gfx/gfx_metal.h").read_text()
    metal_state_h = (ROOT / "src/pc/gfx/gfx_metal_state.h").read_text()
    metal_state_c = (ROOT / "src/pc/gfx/gfx_metal_state.c").read_text()
    metal = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
    main_source = (ROOT / "src/pc/pc_main.m").read_text()
    workflow = (ROOT / ".github/workflows/ios-metal-ipa.yml").read_text()
    ci_script = (ROOT / "ios/build_ios_metal_ci.sh").read_text()

    for token in (
        "gfx_hd_texture_dimensions_compatible",
        "gfx_hd_texture_budget_allows",
        "stbi_info_from_memory",
        "fs_load_write_file(override_path",
        "fs_load_file(original_path",
        "GFX_HD_TEXTURE_MEMORY_BUDGET",
        "gfx_metal_hd_textures_enabled()",
        "gfx_hd_texture_record_residency(texture_id, 0)",
        "load_texture(texname);",
        "(*node)->hd_variant == hd_variant",
        "gfx_hd_texture_refresh_mode()",
    ):
        require(token in graphics, f"missing HD replacement or fallback behavior: {token}")
    require('"hd_textures/gfx/%s.png"' in override,
            "override path must retain the canonical original texture filename")
    require("hd_textures" in metal_state_h, "Metal settings must carry the HD preset flag")
    require("GFX_METAL_PRESET_CLASSIC)\n        settings.hd_textures = false" in metal_state_c,
            "Classic settings must disable HD overrides")
    require("GFX_METAL_PRESET_ENHANCED, 100u, 4u, 8u, true" in metal_state_c,
            "Enhanced preset must enable HD overrides by default")
    for token in ("SM64MetalHDTextures", "gfx_metal_hd_textures_enabled"):
        require(token in metal, f"HD preset state must persist and be queryable: {token}")
    require("bool gfx_metal_hd_textures_enabled(void);" in metal_h,
            "C renderer must query the effective HD setting")
    require("void *fs_load_write_file(const char *relative_path, uint64_t max_size" in fs_h and
            "fs_write_relative_path_is_safe" in fs_c and 'fopen(path, "rb")' in fs_c and
            "fs_open(relative_path)" not in fs_c,
            "replacement reads must be confined to a safe relative path in Documents")
    require("configPrecacheRes = false;" in main_source and
            "bundled-original fallback" in main_source,
            "iOS must load active textures lazily and avoid walking the full pack")
    require("EXTERNAL_DATA=1" in ci_script and "xcodebuild" in ci_script and
            "--require-game-textures" in ci_script,
            "iOS IPA must retain canonical external texture names and bundle originals")
    require("RENDER96" not in workflow + ci_script,
            "third-party pack artwork must not be downloaded or packaged by CI")

    print("iOS HD texture override integration checks passed")


if __name__ == "__main__":
    main()
