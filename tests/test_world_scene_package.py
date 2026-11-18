"""Round-trip original scene fixtures through the native package provider."""
import copy
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.hyrule_importer import write_rgba_png
from tools.world_scene_package import encode_scene


def room(room_id, x, neighbors, textured=False):
    points = [[x, 0, -200], [x, 0, 0], [x + 200, 0, -200]]
    result = {
        "id": room_id, "min": [x, -20, -200], "max": [x + 200, 200, 0],
        "neighbors": neighbors,
        "render_vertices": [{"position": point} for point in points],
        "collision_vertices": points,
        "collision_triangles": [{"indices": [0, 1, 2]}],
    }
    if textured:
        result["texture"] = {"name": "scenes/a.rgba16", "width": 4, "height": 1}
    return result


def entrance(ident, room_id, x):
    return {"id": ident, "room": room_id, "position": [x, 0, -100]}


def make_pair():
    return (
        {"id": 11, "rooms": [room(1, -200, [2], True), room(2, 100, [1])],
         "entrances": [entrance(0, 1, -150), entrance(1, 2, 150)],
         "exits": [{"id": 42, "scene": 12, "entrance": 0}]},
        {"id": 12, "rooms": [room(1, 400, [])], "entrances": [entrance(0, 1, 450)],
         "exits": [{"id": 43, "scene": 11, "entrance": 0}]},
    )


class ScenePackageTest(unittest.TestCase):
    def test_rejects_invalid_authored_input(self):
        scene, _ = make_pair()
        for key, value in (
                ("neighbors", [1]), ("texture", {"width": 4, "height": 1, "name": "../x.rgba16"}),
                ("collision_triangles", [{"indices": [0, 1, 3]}]),
                ("min", [200, 0, 0])):
            with self.subTest(key=key):
                bad = copy.deepcopy(scene)
                bad["rooms"][0][key] = value
                with self.assertRaises(ValueError):
                    encode_scene(bad)

    def test_package_replay(self):
        first, second = make_pair()
        self.assertEqual(encode_scene(first), encode_scene(copy.deepcopy(first)))
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / "scenes").mkdir()
            (work / "scenes/11.wsc").write_bytes(encode_scene(first))
            (work / "scenes/12.wsc").write_bytes(encode_scene(second))
            (work / "scenes/513.wsc").write_bytes(encode_scene({**second, "id": 513, "exits": []}))
            invalid = {**second, "id": 13}
            (work / "scenes/13.wsc").write_bytes(encode_scene(invalid)[:-1])
            # A complete but collinear collision triangle fails registration,
            # after decoding succeeded. The active room must survive.
            degenerate = copy.deepcopy({**second, "id": 14})
            degenerate["rooms"][0]["collision_vertices"] = [[400, 0, -200], [500, 0, -200], [600, 0, -200]]
            (work / "scenes/14.wsc").write_bytes(encode_scene(degenerate))
            write_rgba_png(work / "gfx/scenes/a.rgba16.png", 4, 1, bytes([12, 34, 56, 255] * 4))
            executable = work / "scene-test"
            sources = ["tests/test_world_scene_package.c", "src/engine/world_scene_file.c",
                       "src/engine/world_scene.c", "src/engine/world_region.c", "src/engine/surface_collision.c",
                       "src/pc/gfx/gfx_cc.c", "src/pc/gfx/gfx_scene_capture.c", "src/pc/platform.c",
                       "src/game/area.c"]
            sources.extend(sorted((ROOT / "src/pc/fs").glob("*.c")))
            subprocess.run(shlex.split(os.environ.get("CC", "clang")) + [
                "-std=gnu99", "-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-DF3DEX_GBI_2E", "-DNON_MATCHING", "-DAVOID_UB", "-DVERSION_US", "-DEXTERNAL_DATA",
                "-DIOS_O2R_SCENE_RUNTIME", "-D_LANGUAGE_C", "-Iinclude", "-Isrc", "-I.",
                *sources, "-lm", "-o", executable
            ], cwd=ROOT, check=True)
            subprocess.run([executable, work], cwd=work, check=True)


if __name__ == "__main__":
    unittest.main()
