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
from functools import lru_cache
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


def collision_source_for(defines: set[str]) -> str:
    """Run the host C preprocessor on the real collision stream and fragments."""
    return _preprocessed_collision_source(tuple(sorted(defines)))


@lru_cache(maxsize=4)
def _preprocessed_collision_source(defines: tuple[str, ...]) -> str:
    compiler = shlex.split(os.environ.get("CC", "clang"))
    command = [*compiler, "-E", "-P", "-x", "c", f"-I{ROOT}"]
    command.extend(f"-D{define}" for define in defines)
    command.append(str(ROOT / "levels/castle_grounds/areas/1/collision.inc.c"))
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"C preprocessor failed for {defines}: {result.stderr}")
    return result.stdout


def connected_collision_source() -> str:
    """Expand the real connected-world collision fragments and C conditions."""
    return collision_source_for({"IOS_CONNECTED_WORLD_TEST"})


class HyruleGatewayTests(unittest.TestCase):
    def test_gateway_is_opt_in_and_uses_paired_stock_fading_warps(self):
        castle = (ROOT / "levels/castle_grounds/script.c").read_text()
        hyrule = (ROOT / "levels/hyrule_field/script.c").read_text()
        self.assertRegex(castle, r"#if defined\(IOS_HYRULE_WORLD\)\s*WARP_NODE\([^\n]*0x09[^\n]*LEVEL_HYRULE_FIELD")
        self.assertRegex(hyrule, r"WARP_NODE\([^)]*0x01[^)]*LEVEL_CASTLE_GROUNDS[^)]*0x09")
        self.assertIn("0x28090000, bhvFadingWarp", castle)
        self.assertIn("0x28010000, bhvFadingWarp", hyrule)
        self.assertIn("#if defined(IOS_HYRULE_WORLD) || defined(IOS_CONNECTED_WORLD_TEST)\n"
                      "static const LevelScript script_func_local_5", castle)
        self.assertIn("JUMP_LINK(script_func_local_5)", castle)

    def test_hyrule_still_uses_stock_player_terrain_and_update_loop(self):
        script = (ROOT / "levels/hyrule_field/script.c").read_text()
        level_defines = (ROOT / "levels/level_defines.h").read_text()
        makefile = (ROOT / "Makefile").read_text()
        makefile_split = (ROOT / "Makefile.split").read_text()
        self.assertIn("MARIO(/*model*/ MODEL_MARIO", script)
        self.assertIn("TERRAIN(hyrule_field_collision)", script)
        self.assertIn("CALL_LOOP(/*arg*/ 1, /*func*/ lvl_init_or_update)", script)
        self.assertNotIn("hyrule_update_player", script)
        self.assertNotIn("hyrule_physics_step", script)
        self.assertRegex(level_defines, r"#ifdef IOS_HYRULE_WORLD\s*DEFINE_LEVEL\(\"HYRULE FIELD\"")
        self.assertIn("IOS_HYRULE_WORLD ?= 0", makefile)
        self.assertIn(".ios_world_mode", makefile_split)

    def test_ios_hyrule_diagnostics_cover_warps_spawn_and_collision_activation(self):
        area = (ROOT / "src/game/area.c").read_text()
        level_update = (ROOT / "src/game/level_update.c").read_text()
        diagnostic_guard = "#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD)"
        self.assertIn(diagnostic_guard, area)
        self.assertIn(diagnostic_guard, level_update)
        for event in (
            "Hyrule collision load begin",
            "Hyrule collision load complete",
            "Hyrule collision load failed",
            "SM64 area activated",
        ):
            with self.subTest(event=event):
                self.assertIn(event, area)
        for event in (
            "SM64 level transition begin",
            "SM64 area transition begin",
            "Hyrule level initialized",
            "Hyrule warp spawn",
            "SM64 warp completed",
        ):
            with self.subTest(event=event):
                self.assertIn(event, level_update)
        self.assertIn("gNumStaticSurfaces", area)
        self.assertIn("gNumStaticSurfaceNodes", area)
        self.assertIn("gMarioState->floor", level_update)
        self.assertIn("gCurrentArea->camera", level_update)

    def test_corridor_has_matching_opt_in_geometry_and_collision(self):
        collision = (ROOT / "levels/castle_grounds/areas/1/collision.inc.c").read_text()
        geometry = (ROOT / "levels/castle_grounds/areas/1/forest_gateway.inc.c").read_text()
        self.assertIn("COL_VERTEX_INIT(0x1FA)", collision)
        self.assertIn("COL_VERTEX_INIT(0x1EA)", collision)
        path_vertices = collision.split("// Forest gateway floor stations", 1)[1].split("#endif", 1)[0]
        self.assertEqual(len(re.findall(r"COL_VERTEX\(", path_vertices)), 16)
        corridor_start = collision.index("COL_TRI(490, 492, 491)")
        corridor_end = collision.index("#if defined(IOS_CONNECTED_WORLD_TEST)", corridor_start)
        legacy_corridor_triangles = collision[corridor_start:corridor_end]
        self.assertEqual(len(re.findall(r"COL_TRI\(", legacy_corridor_triangles)), 18)
        self.assertIn("COL_TRI_INIT(SURFACE_DEFAULT, 18)", collision)
        self.assertEqual(len(re.findall(r"\{\{\{", geometry.split("sForestGatewayTrailVertices", 1)[1]
                                           .split("};", 1)[0])), 16)
        self.assertIn("castle_grounds_forest_gateway_dl", (ROOT / "levels/castle_grounds/areas/1/geo.inc.c").read_text())

    def test_castle_entry_overlaps_existing_ground_and_kart_can_be_recovered(self):
        collision = connected_collision_source()
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

    def test_connected_forest_is_rendered_and_collided_in_the_same_castle_area(self):
        script = (ROOT / "levels/castle_grounds/script.c").read_text()
        geo = (ROOT / "levels/castle_grounds/areas/1/geo.inc.c").read_text()
        leveldata = (ROOT / "levels/castle_grounds/leveldata.c").read_text()
        extension = (ROOT / "levels/castle_grounds/areas/1/connected_forest.inc.c").read_text()
        registry = (ROOT / "levels/castle_grounds/areas/1/connected_world/registry.inc.c").read_text()
        area_runtime = (ROOT / "src/game/area.c").read_text()

        self.assertEqual(script.count("AREA(/*index*/ 1"), 1)
        self.assertEqual(script.count("TERRAIN(/*terrainData*/ castle_grounds_seg7_collision_level)"), 1)
        self.assertIn("CALL_LOOP(/*arg*/ 1, /*func*/ lvl_init_or_update)", script)
        self.assertIn("GEO_DISPLAY_LIST(LAYER_OPAQUE, castle_grounds_forest_gateway_dl)", geo)
        self.assertIn("GEO_DISPLAY_LIST(LAYER_OPAQUE, castle_grounds_connected_world_dl)", geo)
        self.assertIn('"levels/castle_grounds/areas/1/connected_world/registry.inc.c"', leveldata)
        self.assertIn("castle_grounds_connected_forest_dl", extension)
        self.assertIn("castle_grounds_north_transition_dl", registry)
        self.assertIn("gsSPDisplayList(castle_grounds_connected_forest_dl)", registry)
        self.assertIn("gsSPDisplayList(castle_grounds_north_transition_dl)", registry)
        self.assertNotIn("bhvFadingWarp", extension)
        self.assertNotIn("LEVEL_HYRULE_FIELD", extension)
        self.assertIn("IOS_CONNECTED_WORLD_TEST ?= 0", (ROOT / "Makefile").read_text())
        self.assertIn("Connected forest Castle area collision loaded", area_runtime)
        self.assertIn("Connected forest test active in existing area", area_runtime)

    def test_connected_forest_floor_supports_a_continuous_out_and_back_route(self):
        source = connected_collision_source()
        old_ground = collision_heights_at(source, -7400, 1460, group="SURFACE_NOISE_DEFAULT")
        self.assertTrue(old_ground, "Castle collision must support the approach")
        self.assertAlmostEqual(old_ground[0], 260, delta=1)

        route = []
        for x in range(-7420, -9201, -40):
            route.append((x, 1460))
        for z in range(2200, 4201, 40):
            route.append((-9200, z))
        for z in range(4200, 8001, 40):
            route.append((-9300, z))
        for z in range(8000, 11601, 40):
            route.append((-9300, z))
        route.extend(reversed(route[:-1]))

        for index, (x, z) in enumerate(route):
            with self.subTest(step=index, x=x, z=z):
                heights = collision_heights_at(source, x, z, group="SURFACE_DEFAULT")
                self.assertTrue(heights, f"no collision floor at route point ({x},{z})")
                self.assertLessEqual(max(heights) - min(heights), 2,
                                     f"overlapping floors disagree at ({x},{z}): {heights}")

        # The branch opening and north extension are unwarped; only the
        # authored outer meadow edges have collision barriers.
        self.assertNotIn("COL_TRI(493, 495, 503)", source)
        self.assertNotIn("COL_TRI(495, 504, 503)", source)
        for triangle in (
            "COL_TRI(495, 504, 506)", "COL_TRI(504, 508, 506)",
            "COL_TRI(493, 507, 503)", "COL_TRI(507, 509, 503)",
            "COL_TRI(506, 524, 512)", "COL_TRI(507, 515, 525)",
        ):
            self.assertIn(triangle, source)

        # The complete static collision count, including the branch, fits the
        # expanded 8,192-surface pool with substantial headroom.
        counts = [int(value) for value in re.findall(r"COL_TRI_INIT\([^,]+,\s*(\d+)\)", source)]
        self.assertIn(48, counts)
        self.assertLess(sum(counts), 8192)

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
            modes = ((), ("-DIOS_HYRULE_WORLD", "-DIOS_BUILD"),
                     ("-DIOS_CONNECTED_WORLD_TEST", "-DIOS_BUILD"),
                     ("-DIOS_HYRULE_WORLD", "-DIOS_CONNECTED_WORLD_TEST", "-DIOS_BUILD"))
            for mode in modes:
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
                [*common, "-DIOS_CONNECTED_WORLD_TEST", "-include", "sm64.h", "-include",
                 "geo_commands.h", str(ROOT / "levels/castle_grounds/areas/1/forest_gateway.inc.c")],
                cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(geometry.returncode, 0, geometry.stdout + geometry.stderr)

            extension = subprocess.run(
                [*common, "-DIOS_CONNECTED_WORLD_TEST", "-include", "sm64.h", "-include",
                 "geo_commands.h", str(ROOT / "levels/castle_grounds/areas/1/connected_forest.inc.c")],
                cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(extension.returncode, 0, extension.stdout + extension.stderr)

            registry = subprocess.run(
                [*common, "-DIOS_CONNECTED_WORLD_TEST", "-include", "sm64.h", "-include",
                 "geo_commands.h", str(ROOT / "levels/castle_grounds/areas/1/connected_world/registry.inc.c")],
                cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(registry.returncode, 0, registry.stdout + registry.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
