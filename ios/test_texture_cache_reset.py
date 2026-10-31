#!/usr/bin/env python3
"""Guard the production cache rollover path against stale bucket roots."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TextureCacheResetTest(unittest.TestCase):
    def test_rollover_clears_hashmap_before_reusing_arena(self):
        source = (ROOT / "src/pc/gfx/gfx_pc.c").read_text()
        rollover = re.search(
            r"if \(gfx_texture_cache\.pool_pos == sizeof\(gfx_texture_cache\.pool\) / sizeof\(struct TextureHashmapNode\)\) \{(?P<body>.*?)\n    \}",
            source,
            re.S,
        )
        self.assertIsNotNone(rollover, "texture cache full-pool branch not found")
        body = rollover.group("body")
        clear_at = body.find("gfx_texture_cache_reset_generation(")
        reuse_at = body.find("node = &gfx_texture_cache.hashmap[hash];")
        self.assertGreaterEqual(clear_at, 0, "rollover must reset stale cache references")
        self.assertGreater(reuse_at, clear_at, "clear must happen before bucket reuse")
        self.assertIn("&gfx_texture_cache.pool_pos", body)
        self.assertIn("return false;", body)

    def test_helper_is_bounded_and_clears_all_bucket_pointers(self):
        helper = (ROOT / "src/pc/gfx/gfx_texture_cache_util.h").read_text()
        self.assertIn("bucket_count > SIZE_MAX / sizeof(void *)", helper)
        self.assertIn("buckets[i] = NULL;", helper)
        self.assertIn("active_tiles[i] = NULL;", helper)
        self.assertIn("*pool_position = 0;", helper)

    def test_rollover_invalidates_and_repairs_active_texture_tiles(self):
        source = (ROOT / "src/pc/gfx/gfx_pc.c").read_text()
        rollover = source[source.find("gfx_texture_cache_reset_generation("):]
        rollover = rollover[: rollover.find("node = &gfx_texture_cache.hashmap[hash];")]
        self.assertIn("rendering_state.textures, 2", rollover)

        draw = source[source.find("static void gfx_sp_tri1("):]
        texture_setup = draw[draw.find("bool textures_ready = false;"):]
        texture_setup = texture_setup[: texture_setup.find("bool use_texture =")]
        self.assertIn("repair_pass < 2", texture_setup)
        self.assertIn("rendering_state.textures[i] == NULL", texture_setup)
        self.assertIn("used_textures[i] && !rendering_state.textures[i]", texture_setup)
        self.assertIn("if (!textures_ready)", texture_setup)

    def test_hardware_logging_is_user_opt_in_and_rate_limited(self):
        graphics = (ROOT / "src/pc/gfx/gfx_pc.c").read_text()
        ui = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
        self.assertIn("GFX_TEXTURE_CACHE_DIAGNOSTICS_ENABLED() &&", graphics)
        self.assertIn("#define GFX_TEXTURE_CACHE_DIAGNOSTICS_ENABLED() gfx_ios_texture_cache_diagnostics_enabled()", graphics)
        self.assertIn("gfx_texture_cache_rollover_count <= 4 || gfx_texture_cache_rollover_count % 16 == 0", graphics)
        self.assertIn('boolForKey:@"SM64TextureCacheDiagnosticsEnabled"', ui)
        self.assertIn('setBool:enabled forKey:@"SM64TextureCacheDiagnosticsEnabled"', ui)
        self.assertIn('Tex log: OFF', ui)
        self.assertIn('Tex log: ON', ui)
        self.assertIn('@selector(toggleTextureCacheDiagnostics)', ui)
        self.assertIn("gfx_texture_cache_failed_binding_count <= 4 || gfx_texture_cache_failed_binding_count % 120 == 0", graphics)


if __name__ == "__main__":
    unittest.main()
