#!/usr/bin/env python3
"""Structural regressions for the opt-in Castle Grounds to Hyrule gateway."""
from __future__ import annotations

import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def collision_heights_at(source: str, point_x: float, point_z: float,
                        *, group: str, appended_vertices_only: bool = False) -> list[float]:
    """Interpolate real source collision triangles at an X/Z sample point."""
    vertices = [tuple(map(int, match.groups())) for match in re.finditer(
        r"COL_VERTEX\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", source)]
    group_pattern = re.compile(
        rf"COL_TRI_INIT\({re.escape(group)},\s*\d+\)(.*?)(?=COL_TRI_INIT\(|COL_TRI_STOP\()", re.S)
    heights = []
    for match in group_pattern.finditer(source):
        for triangle in re.finditer(r"COL_TRI\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", match.group(1)):
            indices = tuple(map(int, triangle.groups()))
            if appended_vertices_only and min(indices) < 490:
                continue
            a, b, c = (vertices[index] for index in indices)
            denominator = ((b[2] - c[2]) * (a[0] - c[0])
                           + (c[0] - b[0]) * (a[2] - c[2]))
            if denominator == 0:
                continue
            wa = ((b[2] - c[2]) * (point_x - c[0])
                  + (c[0] - b[0]) * (point_z - c[2])) / denominator
            wb = ((c[2] - a[2]) * (point_x - c[0])
                  + (a[0] - c[0]) * (point_z - c[2])) / denominator
            wc = 1.0 - wa - wb
            if min(wa, wb, wc) >= -1e-6:
                heights.append(wa * a[1] + wb * b[1] + wc * c[1])
    return heights


class HyruleGatewayTests(unittest.TestCase):
    def test_gateway_is_opt_in_and_uses_paired_stock_fading_warps(self):
        castle = (ROOT / "levels/castle_grounds/script.c").read_text()
        hyrule = (ROOT / "levels/hyrule_field/script.c").read_text()
        self.assertRegex(castle, r"#if defined\(IOS_HYRULE_WORLD\)\s*WARP_NODE\([^\n]*0x09[^\n]*LEVEL_HYRULE_FIELD")
        self.assertRegex(hyrule, r"WARP_NODE\([^)]*0x01[^)]*LEVEL_CASTLE_GROUNDS[^)]*0x09")
        self.assertIn("0x28090000, bhvFadingWarp", castle)
        self.assertIn("0x28010000, bhvFadingWarp", hyrule)
        self.assertIn("#if defined(IOS_HYRULE_WORLD)\nstatic const LevelScript script_func_local_5", castle)
        self.assertIn("JUMP_LINK(script_func_local_5)", castle)

    def test_hyrule_still_uses_stock_player_terrain_and_update_loop(self):
        script = (ROOT / "levels/hyrule_field/script.c").read_text()
        level_defines = (ROOT / "levels/level_defines.h").read_text()
        makefile = (ROOT / "Makefile").read_text()
        self.assertIn("MARIO(/*model*/ MODEL_MARIO", script)
        self.assertIn("TERRAIN(hyrule_field_collision)", script)
        self.assertIn("CALL_LOOP(/*arg*/ 1, /*func*/ lvl_init_or_update)", script)
        self.assertNotIn("hyrule_update_player", script)
        self.assertNotIn("hyrule_physics_step", script)
        self.assertRegex(level_defines, r"#ifdef IOS_HYRULE_WORLD\s*DEFINE_LEVEL\(\"HYRULE FIELD\"")
        self.assertIn("IOS_HYRULE_WORLD ?= 0", makefile)
        self.assertIn(".ios_hyrule_mode", makefile)

    def test_corridor_has_matching_opt_in_geometry_and_collision(self):
        collision = (ROOT / "levels/castle_grounds/areas/1/collision.inc.c").read_text()
        geometry = (ROOT / "levels/castle_grounds/areas/1/forest_gateway.inc.c").read_text()
        self.assertIn("COL_VERTEX_INIT(0x1FA)", collision)
        self.assertIn("COL_VERTEX_INIT(0x1EA)", collision)
        path_vertices = collision.split("// Forest gateway floor stations", 1)[1].split("#endif", 1)[0]
        self.assertEqual(len(re.findall(r"COL_VERTEX\(", path_vertices)), 16)
        corridor_triangles = collision.split("// Six walkable floor triangles", 1)[1].split("#endif", 1)[0]
        self.assertEqual(len(re.findall(r"COL_TRI\(", corridor_triangles)), 18)
        self.assertIn("COL_TRI_INIT(SURFACE_DEFAULT, 18)", corridor_triangles)
        self.assertEqual(len(re.findall(r"\{\{\{", geometry.split("sForestGatewayTrailVertices", 1)[1]
                                           .split("};", 1)[0])), 16)
        self.assertIn("castle_grounds_forest_gateway_dl", (ROOT / "levels/castle_grounds/areas/1/geo.inc.c").read_text())

    def test_castle_entry_overlaps_existing_ground_and_kart_can_be_recovered(self):
        collision = (ROOT / "levels/castle_grounds/areas/1/collision.inc.c").read_text()
        old_ground = collision_heights_at(collision, -7400, 1460,
                                          group="SURFACE_NOISE_DEFAULT")
        self.assertTrue(old_ground, "the original castle collision must cover the path entrance approach")
        self.assertAlmostEqual(old_ground[0], 260, delta=1)

        # The added path begins on the same Y plane as the actual original
        # terrain. Sample the complete route so triangle boundaries cannot
        # leave a collision gap beneath Mario or the kart.
        for x in (-7420, -7800, -8200, -8600, -9200, -9800, -10400, -11100):
            with self.subTest(path_x=x):
                heights = collision_heights_at(collision, x, 1460,
                                               group="SURFACE_DEFAULT",
                                               appended_vertices_only=True)
                self.assertTrue(heights, f"gateway collision does not cover x={x}")
                self.assertAlmostEqual(heights[0], 260, delta=1)

        # The rendered strip must use exactly the collision station positions.
        geometry = (ROOT / "levels/castle_grounds/areas/1/forest_gateway.inc.c").read_text()
        trail = geometry.split("sForestGatewayTrailVertices", 1)[1].split("};", 1)[0]
        rendered_x = [int(value) for value in re.findall(r"\{\{\{(-?\d+),\s*263,", trail)]
        self.assertEqual(rendered_x, [-7420] * 4 + [-8600] * 4 + [-9800] * 4 + [-11100] * 4)

        hyrule = (ROOT / "levels/hyrule_field/script.c").read_text()
        self.assertIn("MODEL_IOS_KART, HYRULE_FIELD_KART_X", hyrule)
        self.assertIn("bhvIOSKart", hyrule)

    def test_gateway_c_and_warp_sources_compile_with_feature_on_and_off(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        if not compiler or shutil.which(compiler[0]) is None:
            self.skipTest("host C compiler is unavailable")
        common = [
            *compiler, "-fsyntax-only", "-std=gnu99", "-D_LANGUAGE_C",
            "-DF3DEX_GBI_2", "-DNON_MATCHING=1", "-DAVOID_UB=1",
            f"-I{ROOT / 'include'}", f"-I{ROOT / 'src'}", f"-I{ROOT}",
        ]
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            generated_include = temp / "build" / "hyrule_field_spawn.h"
            generated_include.parent.mkdir()
            generated_include.write_text(
                "#define HYRULE_FIELD_SPAWN_X 160\n"
                "#define HYRULE_FIELD_SPAWN_Y 0\n"
                "#define HYRULE_FIELD_SPAWN_Z 1415\n"
                "#define HYRULE_FIELD_SPAWN_YAW ((s16)-20)\n"
                "#define HYRULE_FIELD_KART_X -340\n"
                "#define HYRULE_FIELD_KART_Y 0\n"
                "#define HYRULE_FIELD_KART_Z 1415\n"
                "#define HYRULE_FIELD_RETURN_X 960\n"
                "#define HYRULE_FIELD_RETURN_Y 0\n"
                "#define HYRULE_FIELD_RETURN_Z 1415\n",
                encoding="utf-8")
            collision_tu = temp / "castle_collision.c"
            collision_tu.write_text(
                "#include <ultra64.h>\n"
                '#include "sm64.h"\n'
                '#include "surface_terrains.h"\n'
                '#include "level_misc_macros.h"\n'
                '#include "macro_preset_names.h"\n'
                '#include "special_preset_names.h"\n'
                '#include "levels/castle_grounds/areas/1/collision.inc.c"\n',
                encoding="utf-8")
            for enabled in (False, True):
                mode = ["-DIOS_HYRULE_WORLD", "-DIOS_BUILD"] if enabled else []
                collision = subprocess.run(
                    [*common, *mode, str(collision_tu)], text=True, capture_output=True)
                self.assertEqual(collision.returncode, 0, collision.stdout + collision.stderr)
                scripts = subprocess.run(
                    [*compiler, "-I" + str(temp), *common[1:], *mode, "-DIOS_BUILD",
                     str(ROOT / "levels/castle_grounds/script.c"),
                     str(ROOT / "levels/hyrule_field/script.c")],
                    cwd=ROOT, text=True, capture_output=True)
                self.assertEqual(scripts.returncode, 0, scripts.stdout + scripts.stderr)

            geometry = subprocess.run(
                [*common, "-DIOS_HYRULE_WORLD", "-include", "sm64.h", "-include",
                 "geo_commands.h", str(ROOT / "levels/castle_grounds/areas/1/forest_gateway.inc.c")],
                cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(geometry.returncode, 0, geometry.stdout + geometry.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
