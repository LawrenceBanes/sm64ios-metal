#!/usr/bin/env python3
"""Geometry, collision, and build-mode checks for the opt-in Castle forest."""
from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_hyrule_gateway import collision_heights_at, collision_source_for  # noqa: E402


def vertices(source: str) -> list[tuple[int, int, int]]:
    return [tuple(map(int, match.groups())) for match in re.finditer(
        r"COL_VERTEX\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", source)]


def vertex_array(source: str, name: str) -> list[tuple[int, int, int]]:
    body = source.split(name, 1)[1].split("};", 1)[0]
    return [tuple(map(int, match.groups())) for match in re.finditer(
        r"\{\{\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}", body)]


def triangle_groups(source: str) -> list[tuple[str, int, list[tuple[int, int, int]]]]:
    matches = list(re.finditer(r"COL_TRI_INIT\(([^,]+),\s*(\d+)\)", source))
    groups = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else source.index("COL_TRI_STOP()")
        body = source[match.end():end]
        triangles = [tuple(map(int, tri.groups())) for tri in re.finditer(
            r"COL_TRI(?:_SPECIAL)?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)", body)]
        groups.append((match.group(1).strip(), int(match.group(2)), triangles))
    return groups


def interpolate_floor(source: str, x: float, z: float) -> float:
    heights = collision_heights_at(source, x, z, group="SURFACE_DEFAULT")
    if not heights:
        raise AssertionError(f"no connected collision floor at ({x}, {z})")
    if max(heights) - min(heights) > 2:
        raise AssertionError(f"overlapping floors disagree at ({x}, {z}): {heights}")
    return heights[0]


def normal_y(a: tuple[int, int, int], b: tuple[int, int, int],
             c: tuple[int, int, int]) -> int:
    ab = tuple(b[i] - a[i] for i in range(3))
    ac = tuple(c[i] - a[i] for i in range(3))
    return ab[2] * ac[0] - ab[0] * ac[2]


def normal_x(a: tuple[int, int, int], b: tuple[int, int, int],
             c: tuple[int, int, int]) -> int:
    ab = tuple(b[i] - a[i] for i in range(3))
    ac = tuple(c[i] - a[i] for i in range(3))
    return ab[1] * ac[2] - ab[2] * ac[1]


class ConnectedWorldTests(unittest.TestCase):
    def setUp(self):
        self.stock = collision_source_for(set())
        self.hyrule = collision_source_for({"IOS_HYRULE_WORLD"})
        self.connected = collision_source_for({"IOS_CONNECTED_WORLD_TEST"})

    def test_feature_modes_keep_original_counts_and_do_not_mix_world_fragments(self):
        expected = ((self.stock, 0x1EA), (self.hyrule, 0x1FA), (self.connected, 0x210))
        for source, count in expected:
            with self.subTest(vertex_count=count):
                match = re.search(r"COL_VERTEX_INIT\((0x[0-9A-Fa-f]+|\d+)\)", source)
                self.assertIsNotNone(match)
                self.assertEqual(int(match.group(1), 0), count)
                self.assertEqual(len(vertices(source)), count)
                groups = triangle_groups(source)
                for name, declared, triangles in groups:
                    with self.subTest(surface=name, declared=declared):
                        self.assertEqual(len(triangles), declared)
                        for tri in triangles:
                            self.assertTrue(all(0 <= vertex < count for vertex in tri), tri)

        self.assertNotIn("connected_world/collision_vertices.inc.c", self.stock)
        self.assertNotIn("connected_world/collision_vertices.inc.c", self.hyrule)
        self.assertIn("COL_TRI_INIT(SURFACE_DEFAULT, 48)", self.connected)
        self.assertIn("IOS_CONNECTED_WORLD_TEST ?= 0", (ROOT / "Makefile").read_text())
        self.assertIn("IOS_HYRULE_WORLD ?= 0", (ROOT / "Makefile").read_text())

    def test_connected_vertices_fit_signed_coordinates_and_expanded_grid(self):
        points = vertices(self.connected)
        self.assertEqual(len(points), 0x210)
        for index, (x, y, z) in enumerate(points):
            with self.subTest(index=index, point=(x, y, z)):
                self.assertGreaterEqual(x, -32768)
                self.assertLess(x, 32768)
                self.assertGreaterEqual(y, -32768)
                self.assertLess(y, 32768)
                self.assertGreaterEqual(z, -32768)
                self.assertLess(z, 32768)
                self.assertIn((x + 32768) // 1024, range(64))
                self.assertIn((z + 32768) // 1024, range(64))

    def test_collision_surface_counts_and_all_indices_match_the_stream(self):
        count = len(vertices(self.connected))
        groups = triangle_groups(self.connected)
        connected_default = [group for group in groups if group[0] == "SURFACE_DEFAULT"]
        self.assertTrue(any(declared == 48 for _, declared, _ in connected_default))
        self.assertLess(sum(declared for _, declared, _ in groups), 8192)
        for name, declared, triangles in groups:
            self.assertEqual(len(triangles), declared, name)
            for triangle in triangles:
                self.assertEqual(len(set(triangle)), 3, (name, triangle))
                self.assertTrue(all(0 <= index < count for index in triangle),
                                (name, triangle, count))

    def test_new_walkable_faces_have_nonzero_area_and_upward_winding(self):
        points = vertices(self.connected)
        extension = (ROOT / "levels/castle_grounds/areas/1/connected_world/collision_surfaces.inc.c").read_text()
        floor_source = extension.split("// Low outer walls", 1)[0]
        triangles = [tuple(map(int, match.groups())) for match in re.finditer(
            r"COL_TRI\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", floor_source)]
        self.assertEqual(len(triangles), 16)
        for triangle in triangles:
            a, b, c = (points[index] for index in triangle)
            with self.subTest(triangle=triangle):
                self.assertGreater(normal_y(a, b, c), 0,
                                   f"walkable face must wind upward: {triangle}")

        all_extension_triangles = [tuple(map(int, match.groups())) for match in re.finditer(
            r"COL_TRI\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", extension)]
        west = all_extension_triangles[16:20]
        east = all_extension_triangles[20:24]
        self.assertEqual((len(west), len(east)), (4, 4))
        for triangle in west:
            self.assertGreater(normal_x(*(points[index] for index in triangle)), 0,
                               f"west wall must face inward: {triangle}")
        for triangle in east:
            self.assertLess(normal_x(*(points[index] for index in triangle)), 0,
                            f"east wall must face inward: {triangle}")

    def test_display_list_vertex_windows_are_bounded_and_indices_are_local(self):
        sources = (
            ROOT / "levels/castle_grounds/areas/1/connected_forest.inc.c",
            ROOT / "levels/castle_grounds/areas/1/connected_world/north_transition.inc.c",
        )
        for path in sources:
            text = path.read_text()
            loaded_vertices = None
            for line_number, line in enumerate(text.splitlines(), 1):
                vertex = re.search(r"gsSPVertex\([^,]+,\s*(\d+)\s*,\s*(\d+)\)", line)
                if vertex:
                    loaded_vertices = int(vertex.group(1))
                    start = int(vertex.group(2))
                    with self.subTest(file=path.name, line=line_number):
                        self.assertLessEqual(loaded_vertices, 32)
                        self.assertGreaterEqual(start, 0)
                    continue
                if "gsSP2Triangles(" in line:
                    args = [int(value.strip(), 0) for value in re.search(
                        r"gsSP2Triangles\(([^)]*)\)", line).group(1).split(",")]
                    self.assertEqual(len(args), 8)
                    self.assertIsNotNone(loaded_vertices, (path.name, line_number))
                    for index in (args[0], args[1], args[2], args[4], args[5], args[6]):
                        with self.subTest(file=path.name, line=line_number, index=index):
                            self.assertLess(index, loaded_vertices)
                if "gsSP1Triangle(" in line:
                    args = [int(value.strip(), 0) for value in re.search(
                        r"gsSP1Triangle\(([^)]*)\)", line).group(1).split(",")]
                    self.assertEqual(len(args), 4)
                    self.assertIsNotNone(loaded_vertices, (path.name, line_number))
                    for index in args[:3]:
                        with self.subTest(file=path.name, line=line_number, index=index):
                            self.assertLess(index, loaded_vertices)

    def test_main_trail_loop_and_north_corridor_have_continuous_floor(self):
        route = []
        route.extend((-x, 1460) for x in range(7420, 9201, 40))
        route.extend((-9200, z) for z in range(2200, 4201, 40))
        route.extend((-9300, z) for z in range(4200, 11601, 40))
        route.extend(reversed(route[:-1]))
        sampled_heights = [interpolate_floor(self.connected, x, z) for x, z in route]
        self.assertLessEqual(max(abs(b - a) for a, b in zip(sampled_heights, sampled_heights[1:])), 2)

        loop_centers = ((-9700, 6200), (-10400, 6800), (-10500, 7300),
                        (-10100, 7700), (-9700, 8000))
        for start, end in zip(loop_centers, loop_centers[1:]):
            steps = max(abs(end[0] - start[0]), abs(end[1] - start[1])) // 20
            for step in range(steps + 1):
                t = step / steps
                x = start[0] + (end[0] - start[0]) * t
                z = start[1] + (end[1] - start[1]) * t
                interpolate_floor(self.connected, x, z)

    def test_rendered_floor_vertices_follow_collision_and_visual_lift(self):
        render = (ROOT / "levels/castle_grounds/areas/1/connected_forest.inc.c").read_text()
        collision = vertices(self.connected)
        meadow = vertex_array(render, "sConnectedMeadowVertices")
        expected_indices = (506, 510, 511, 507, 512, 513, 514, 515, 516, 517, 518, 519)
        self.assertEqual(len(meadow), len(expected_indices))
        for rendered, index in zip(meadow, expected_indices):
            x, y, z = rendered
            cx, cy, cz = collision[index]
            with self.subTest(rendered=rendered, collision_index=index):
                self.assertEqual((x, z), (cx, cz))
                self.assertEqual(y, cy + 3)

        for filename, array_name in (
            (render, "sConnectedMainTrailVertices"),
            (render, "sConnectedLoopTrailVertices"),
            ((ROOT / "levels/castle_grounds/areas/1/connected_world/north_transition.inc.c").read_text(),
             "sConnectedNorthGroundVertices"),
            ((ROOT / "levels/castle_grounds/areas/1/connected_world/north_transition.inc.c").read_text(),
             "sConnectedNorthTrailVertices"),
        ):
            for x, y, z in vertex_array(filename, array_name):
                with self.subTest(array=array_name, point=(x, y, z)):
                    floor_y = interpolate_floor(self.connected, x, z)
                    self.assertAlmostEqual(y - floor_y, 3, delta=1)

        for x, y, z in re.findall(r"CONNECTED_PINE\((-?\d+),\s*(-?\d+),\s*(-?\d+)\)", render):
            x, y, z = int(x), int(y), int(z)
            with self.subTest(pine=(x, y, z)):
                self.assertAlmostEqual(y - interpolate_floor(self.connected, x, z), 1, delta=1)

        landmark = vertex_array(render, "sConnectedLandmarkVertices")
        for x, y, z in landmark[:4]:
            with self.subTest(landmark_base=(x, y, z)):
                self.assertAlmostEqual(y - interpolate_floor(self.connected, x, z), 1, delta=1.5)

        marker_source = (ROOT / "levels/castle_grounds/areas/1/connected_world/north_transition.inc.c").read_text()
        marker = vertex_array(marker_source, "sConnectedNorthMarkerVertices")
        for x, y, z in marker[:2] + marker[4:6]:
            with self.subTest(north_marker_base=(x, y, z)):
                self.assertAlmostEqual(y - interpolate_floor(self.connected, x, z), 0, delta=1.5)

    def test_world_stays_one_stock_gameplay_area_with_a_module_root(self):
        script = (ROOT / "levels/castle_grounds/script.c").read_text()
        geo = (ROOT / "levels/castle_grounds/areas/1/geo.inc.c").read_text()
        leveldata = (ROOT / "levels/castle_grounds/leveldata.c").read_text()
        registry = (ROOT / "levels/castle_grounds/areas/1/connected_world/registry.inc.c").read_text()
        self.assertEqual(script.count("AREA(/*index*/ 1"), 1)
        self.assertEqual(script.count("TERRAIN(/*terrainData*/ castle_grounds_seg7_collision_level)"), 1)
        self.assertIn("CALL_LOOP(/*arg*/ 1, /*func*/ lvl_init_or_update)", script)
        self.assertEqual(geo.count("castle_grounds_connected_world_dl"), 1)
        self.assertIn('"levels/castle_grounds/areas/1/connected_world/registry.inc.c"', leveldata)
        self.assertIn("gsSPDisplayList(castle_grounds_connected_forest_dl)", registry)
        self.assertIn("gsSPDisplayList(castle_grounds_north_transition_dl)", registry)
        self.assertNotIn("LEVEL_HYRULE_FIELD", registry)
        self.assertNotIn("bhvFadingWarp", registry)
        forest = (ROOT / "levels/castle_grounds/areas/1/connected_forest.inc.c").read_text()
        north = (ROOT / "levels/castle_grounds/areas/1/connected_world/north_transition.inc.c").read_text()
        triangle_budget = sum(2 * len(re.findall(r"\bgsSP2Triangles\(", source))
                              + len(re.findall(r"\bgsSP1Triangle\(", source))
                              for source in (forest, north))
        self.assertEqual(triangle_budget, 70)
        render_vertex_budget = (
            sum(len(vertex_array(forest, name)) for name in (
                "sConnectedClearingVertices", "sConnectedMeadowVertices",
                "sConnectedMainTrailVertices", "sConnectedLoopTrailVertices",
                "sConnectedLandmarkVertices"))
            + 10 * len(re.findall(r"CONNECTED_PINE\(-?\d+,\s*-?\d+,\s*-?\d+\)", forest))
            + sum(len(vertex_array(north, name)) for name in (
                "sConnectedNorthGroundVertices", "sConnectedNorthTrailVertices",
                "sConnectedNorthMarkerVertices")))
        self.assertEqual(render_vertex_budget, 109)
        self.assertEqual(registry.count("gsSPDisplayList("), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
