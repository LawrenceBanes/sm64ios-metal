"""Round-trip original scene fixtures through the native package provider."""
import copy
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.hyrule_importer import write_rgba_png
from tools.world_scene_package import decode_indexed_rgba, encode_scene, texture_descriptor
from tools.scene_diagnostic import authored_wsc3_scene, generate
from ios.validate_ipa import check_scene_diagnostic
from test_o2r_to_wsc3 import export_fixture


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


def material_scene():
    first, _ = make_pair()
    first.update(id=21, version=2, exits=[])
    west, east = first["rooms"]
    vertices = west.pop("render_vertices")
    west.pop("texture")
    for vertex in vertices:
        vertex["color"] = [255, 255, 255, 128]
    texture = {"name": "scenes/indexed.rgba16", "width": 4, "height": 2}
    west["states"] = [{"scroll": [32, -16], "tint": [255, 128, 0, 96]}]
    west["batches"] = [
        {"render_vertices": vertices, "pass": "translucent", "texture": texture, "state": 1},
        {"render_vertices": vertices, "pass": "opaque", "texture": texture},
        {"render_vertices": vertices, "pass": "translucent"},
    ]
    east["batches"] = [{"render_vertices": east.pop("render_vertices")}]
    first["indexed_textures"] = [{**texture, "index_bits": 8, "indices": "0001020001000201",
                                  "palette_format": "rgba5551", "palette": "f80107c04211"}]
    return first


class ScenePackageTest(unittest.TestCase):
    def test_texture_limit_uses_four_thousand_ninety_six_texels(self):
        supported = {"name": "scenes/hyrule.rgba16", "width": 128, "height": 32}
        self.assertEqual(texture_descriptor(supported, 5)[:2], (128, 32))
        with self.assertRaisesRegex(ValueError, "dimensions"):
            texture_descriptor(supported, 4)
        with self.assertRaisesRegex(ValueError, "dimensions"):
            texture_descriptor({**supported, "height": 33}, 5)

    def test_palette_formats_and_bounds(self):
        base = {"name": "scenes/unit.rgba16", "width": 4, "height": 1,
                "index_bits": 4, "indices": "0101", "palette_format": "ia88", "palette": "6480ff00"}
        self.assertEqual(decode_indexed_rgba(base), bytes([100, 100, 100, 128, 255, 255, 255, 0] * 2))
        rgba8 = material_scene()["indexed_textures"][0]
        self.assertEqual(decode_indexed_rgba(rgba8)[:12], bytes([255, 0, 0, 255, 0, 255, 0, 0, 66, 66, 66, 255]))
        for edit in ({"indices": "ff"}, {"indices": "f101"}, {"palette": "abcdff"},
                     {"index_bits": 5}, {"palette_format": "unknown"}, {"palette": "abcd" * 17}):
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                decode_indexed_rgba({**base, **edit})

    def test_author_rejects_bad_state_index_and_pass(self):
        for field, value in (("state", 2), ("pass", "shadow")):
            scene = material_scene()
            scene["rooms"][0]["batches"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                encode_scene(scene)

    def test_wsc3_rejects_unsupported_sampler_and_material(self):
        for changes in ({"fog": True}, {"cutout": 1}, {"alpha_period": 1},
                        {"textures": [None, {}]}, {"textures": [{"name": "scenes/x.rgba16",
                                                                 "width": 4, "height": 1,
                                                                 "address": ["repeat", "mask"]}]}):
            scene = authored_wsc3_scene(4099)
            scene["rooms"][0]["batches"][1]["material"].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                encode_scene(scene)

    def test_wsc5_supports_202_batches_without_raising_older_limits(self):
        scene = copy.deepcopy(authored_wsc3_scene(28))
        scene["version"] = 5
        room = scene["rooms"][0]
        batch = copy.deepcopy(room["batches"][0])
        batch["render_vertices"] = batch["render_vertices"][:3]
        room["batches"] = [copy.deepcopy(batch) for _ in range(202)]
        self.assertEqual(encode_scene(scene)[:4], b"WSC5")

        legacy = copy.deepcopy(scene)
        legacy["version"] = 4
        with self.assertRaisesRegex(ValueError, "batches must contain 1 to 64"):
            encode_scene(legacy)

        oversized = copy.deepcopy(scene)
        oversized["rooms"][0]["batches"] = [copy.deepcopy(batch) for _ in range(257)]
        with self.assertRaisesRegex(ValueError, "batches must contain 1 to 256"):
            encode_scene(oversized)

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
            content = material_scene()
            authored = work / "authored.json"
            authored.write_text(json.dumps(content), encoding="utf-8")
            subprocess.run([sys.executable, ROOT / "tools/world_scene_package.py", authored,
                            work / "scenes/21.wsc", "--texture-root", work], check=True)
            self.assertEqual((work / "scenes/21.wsc").read_bytes(), encode_scene(content))
            fog_scene = copy.deepcopy(authored_wsc3_scene(25))
            fog_scene["version"] = 4
            fog_scene["indexed_textures"] = []
            fog_room = fog_scene["rooms"][0]
            fog_room["batches"] = [fog_room["batches"][0]]
            fog_room["batches"][0]["material"]["fog"] = {
                "multiplier": 128, "offset": 32, "color": [170, 187, 204, 255]}
            (work / "scenes/25.wsc").write_bytes(encode_scene(fog_scene))
            legacy_fog_scene = copy.deepcopy(fog_scene)
            legacy_fog_scene["version"] = 3
            with self.assertRaisesRegex(ValueError, "requires WSC4"):
                encode_scene(legacy_fog_scene)
            decal_scene = copy.deepcopy(authored_wsc3_scene(26))
            decal_scene["version"] = 5
            decal_scene["indexed_textures"] = []
            decal_room = decal_scene["rooms"][0]
            decal_room["batches"] = [decal_room["batches"][0]]
            decal_room["batches"][0]["material"]["decal"] = True
            decal_room["batches"][0]["material"]["blend"] = True
            decal_room["batches"][0]["material"]["depth_write"] = False
            (work / "scenes/26.wsc").write_bytes(encode_scene(decal_scene))
            decal_bytes = (work / "scenes/26.wsc").read_bytes()
            self.assertEqual(decal_bytes[:4], b"WSC5")
            self.assertTrue(struct.unpack_from("<H", decal_bytes, 14 + 26 + 4)[0] & 0x50 == 0x50)
            legacy_decal_scene = copy.deepcopy(decal_scene)
            legacy_decal_scene["version"] = 4
            with self.assertRaisesRegex(ValueError, "requires WSC5"):
                encode_scene(legacy_decal_scene)
            legacy_decal_scene["rooms"][0]["batches"][0]["material"].pop("decal")
            legacy_decal_scene["rooms"][0]["batches"][0]["material"].pop("blend")
            bad = bytearray(encode_scene({**legacy_decal_scene, "id": 27}))
            bad[14 + 26 + 4] |= 0x40
            (work / "scenes/27.wsc").write_bytes(bad)
            bad = bytearray(encode_scene({**content, "id": 22}))
            # First room's first batch follows package/room headers, one
            # neighbor and one draw-state slot. Forge an out-of-range slot.
            bad[14 + 26 + 4 + 8 + 2] = 2
            (work / "scenes/22.wsc").write_bytes(bad)
            bad_asset = copy.deepcopy({**content, "id": 23})
            bad_asset["rooms"][0]["batches"][0]["texture"]["height"] = 1
            (work / "scenes/23.wsc").write_bytes(encode_scene(bad_asset))
            bad = bytearray(encode_scene(authored_wsc3_scene(24)))
            # Unknown material flags must fail without replacing the active room.
            bad[14 + 26 + 4] |= 0x80
            (work / "scenes/24.wsc").write_bytes(bad)
            wide = copy.deepcopy(authored_wsc3_scene(28))
            wide["version"] = 5
            wide_batch = copy.deepcopy(wide["rooms"][0]["batches"][0])
            wide_batch["render_vertices"] = wide_batch["render_vertices"][:3]
            wide["rooms"][0]["batches"] = [copy.deepcopy(wide_batch) for _ in range(202)]
            (work / "scenes/28.wsc").write_bytes(encode_scene(wide))
            high_texture = {"name": "scenes/high.rgba16", "width": 128, "height": 32}
            high_res_scene = copy.deepcopy(authored_wsc3_scene(29))
            high_res_scene["version"] = 5
            high_res_scene["indexed_textures"] = []
            high_res_scene["rooms"][0]["batches"] = [high_res_scene["rooms"][0]["batches"][0]]
            high_res_scene["rooms"][0]["batches"][0]["material"]["textures"] = [high_texture]
            (work / "scenes/29.wsc").write_bytes(encode_scene(high_res_scene))
            write_rgba_png(work / "gfx/scenes/high.rgba16.png", 128, 32, bytes([255, 255, 255, 255] * (128 * 32)))
            legacy_high_res = bytearray(encode_scene({**high_res_scene, "id": 30}))
            legacy_high_res[:4] = b"WSC4"
            (work / "scenes/30.wsc").write_bytes(legacy_high_res)
            fixture = work / "generated"
            generate(fixture)
            packed = work / "base.zip"
            subprocess.run([sys.executable, ROOT / "tools/mkzip.py", fixture / "basepack.lst", packed], check=True)
            with zipfile.ZipFile(packed) as archive:
                check_scene_diagnostic(archive)
                self.assertEqual(len(archive.namelist()), 7)
                generate(fixture)
                for line in (fixture / "basepack.lst").read_text().splitlines():
                    local, member = line.split()
                    self.assertEqual(Path(local).read_bytes(), archive.read(member))
            imported = export_fixture(work)
            (work / "scenes/4100.wsc").write_bytes((imported / "scenes/4100.wsc").read_bytes())
            imported_textures = sorted((imported / "gfx/scenes").glob("*.png"))
            for path in imported_textures:
                (work / "gfx/scenes" / path.name).write_bytes(path.read_bytes())
            executable = work / "scene-test"
            sources = ["tests/test_world_scene_package.c", "src/engine/world_scene_file.c",
                       "src/engine/world_scene.c", "src/engine/world_region.c", "src/engine/surface_collision.c",
                       "src/pc/gfx/gfx_cc.c", "src/pc/gfx/gfx_scene_capture.c", "src/pc/platform.c",
                       "src/game/area.c", "src/pc/gfx/gfx_metal_shader.c"]
            sources.extend(sorted((ROOT / "src/pc/fs").glob("*.c")))
            subprocess.run(shlex.split(os.environ.get("CC", "clang")) + [
                "-std=gnu99", "-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-DF3DEX_GBI_2E", "-DNON_MATCHING", "-DAVOID_UB", "-DVERSION_US", "-DEXTERNAL_DATA",
                "-DIOS_O2R_SCENE_RUNTIME", "-DIOS_SCENE_DIAGNOSTIC", "-D_LANGUAGE_C", "-Iinclude", "-Isrc", "-I.",
                *sources, "-lm", "-o", executable
            ], cwd=ROOT, check=True)
            subprocess.run([executable, work, packed, work / "gfx/scenes" / imported_textures[0].name],
                           cwd=work, check=True)


if __name__ == "__main__":
    unittest.main()
