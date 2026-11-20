"""Generate original O2R resources and exercise the production converter."""
from fractions import Fraction
import json
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from test_o2r_draw_trace import bounds, display_list, packet, ref, resource, scene, text, tile
from tools import o2r_draw_trace as trace
from tools.o2r_to_wsc3 import (convert_archive, crop_ci4, fold_static_primitive_lod_alpha,
                               lower_material, transform_position)
from tools.o2r_to_wsc3 import alpha_bytes_are_opaque

ROOT = "workshop/scene"
ROOMS = ("workshop/west", "workshop/east")


def texture(fmt, width, height, data):
    return resource("OTEX", struct.pack(">4I", fmt, width, height, len(data)) + data)


def matrix_resource(values):
    flat = [value for row in values for value in row]
    fixed = [round(value * 65536) for value in flat]
    return resource("OMTX", struct.pack(">16h16H", *(value >> 16 for value in fixed),
                                       *(value & 0xFFFF for value in fixed)))


def floor_points(a, b, y=0):
    return [(a, y, -240), (a, y, 240), (b, y, 240), (b, y, -240)]


def collision(polygons, vertex_flags=(0, 0)):
    points, indices = [], []
    for quad in polygons:
        base = len(points)
        points.extend(quad)
        indices.extend((base, base + n, base + n + 1) for n in (1, 2))
    low = [min(coordinate) for coordinate in zip(*points)]
    high = [max(coordinate) for coordinate in zip(*points)]
    vertices = b"".join(struct.pack(">3h", *point) for point in points)
    faces = b"".join(struct.pack(">4H4h", 0,
                                   (vertex_flags[0] << 13) | tri[0],
                                   (vertex_flags[1] << 13) | tri[1], tri[2],
                                   0, 32767, 0, 0) for tri in indices)
    metadata = struct.pack(">6I", 1, 0, 0, 0, 0, 0)
    return resource("OCOL", struct.pack(">6hI", *low, *high, len(points)) + vertices
                    + struct.pack(">I", len(indices)) + faces + metadata)


def header(shared):
    spawns = struct.pack(">I8h8h", 2, 0, -180, 12, 0, 0, 0, 0, 0,
                         0, 180, 12, 0, 0, -0x4000, 0, 0)
    room_table = struct.pack(">I", 2) + b"".join(text(path) + bytes(8) for path in ROOMS)
    transition = struct.pack(">I4B6h", 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0)
    commands = [(0, spawns), (4, room_table), (6, struct.pack(">I4b", 2, 0, 0, 1, 1)), (14, transition)]
    if shared:
        commands.append((3, text("workshop/collision")))
    return scene(*commands)


def fixture(path, *, shared=False, fog=False, incomplete_fog=False, lighting=False,
            format_error=False, crop=None, transform=False, collision_flags=(0, 0)):
    # The outer image border is red. A byte-exact crop of the inner rows
    # has blue pixels and a green bottom row, making stride mistakes visible.
    pixels = b"".join(b"\x07\xc1" if 2 <= x < 6 and y == 4 else b"\x00\x3f"
                      if 2 <= x < 6 and 1 <= y < 4 else b"\xf8\x01"
                      for y in range(6) for x in range(8))
    if crop == "ci4-odd":
        crop_upload = (ref(0x20, "workshop/inlay", (2 << 21) | 7),
                       tile(7, 8, fmt=2, size=0, line=1),
                       packet(0xF4, (4 << 12) | 4, (7 << 24) | (16 << 12) | 16))
    else:
        crop_upload = (ref(0x20, "workshop/inlay", (2 << 19) | 7),
                       tile(7, 8, fmt=0, size=2, line=2 if crop == "wrong-line" else 1),
                       packet(0xF4, (8 << 12) | 4, (7 << 24) | (20 << 12) | 16))
    block_upload = (ref(0x20, "workshop/inlay", (2 << 19) | 3), tile(7, 8, fmt=0, size=2),
                    packet(0xF3, 0, (7 << 24) | (15 << 12) | 0x800))
    with ZipFile(path, "w") as archive:
        archive.writestr(ROOT, header(shared))
        archive.writestr("workshop/rug", texture(3, 4, 4, bytes.fromhex("0101010101010101")))
        palette = bytes.fromhex("f80107c1003fffff") + bytes(24)
        archive.writestr("workshop/palette", texture(2, 16, 1, palette))
        if transform:
            archive.writestr("workshop/translate", matrix_resource(
                ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (100, 0, 0, 1))))
        if crop:
            if crop == "ci4-odd":
                indices = [(x + 2 * y) % 4 for y in range(6) for x in range(8)]
                packed = bytes((indices[i] << 4) | (indices[i + 1] if i + 1 < len(indices) else 0)
                               for i in range(0, len(indices), 2))
                archive.writestr("workshop/inlay", texture(3, 8, 6, packed))
            else:
                archive.writestr("workshop/inlay", texture(2, 8, 6, pixels))
        else:
            archive.writestr("workshop/inlay", texture(2 if not format_error else 9, 4, 4, bytes.fromhex("003f" * 16)))
        quads = []
        for i, (room, (a, b)) in enumerate(zip(ROOMS, ((-400, 0), (0, 400)))):
            quad = floor_points(a, b)
            quads.append(quad)
            vtx, opaque, xlu = room + "/vtx", room + "/opa", room + "/xlu"
            archive.writestr(vtx, resource("OVTX", struct.pack(">I", 4) + b"".join(
                struct.pack(">3hH2h4B", *point, 0, 64 if index >= 2 else -32, index * 12,
                            200, 180, 160, 128) for index, point in enumerate(quad))))
            archive.writestr(opaque, display_list(
                packet(0xD9, 0, 0x10001 if fog else 1),
                packet(0xEF, 0x188000, 0x30), packet(0xD7, 2, 0xFFFF8000),
                packet(0xFB, 0, 0xFFFFFF80),
                packet(0xFC, (2 << 20) | (12 << 15), (1 << 28) | (1 << 15)),
                *((packet(0xDB, 8 << 16, (128 << 16) | 32), packet(0xF8, 0, 0xAABBCCFF))
                  if fog and not incomplete_fog else ()),
                *((packet(0xFC, 0, (4 << 15) | (5 << 9)),) if lighting else ()),
                *((packet(0xD9, 0x00FDFFFF, 0x00020000), packet(0xDB, 2 << 16, 24),
                   ref(0x42, "workshop/light-directional", 0x0010060A),
                   ref(0x42, "workshop/light-ambient", 0x0010090A)) if lighting else ()),
                *(((packet(0xDA, 2, 0x08000001) if transform == "segmented"
                    else ref(0x36, "workshop/translate", 2)),) if transform else ()),
                ref(0x32, vtx, 0x4008),
                ref(0x20, "workshop/palette", (2 << 19) | 15), tile(7, 256, fmt=0, size=2),
                packet(0xF0, 0, (7 << 24) | (15 << 14)),
                ref(0x20, "workshop/rug", 2 << 19), tile(7, 0, size=2),
                packet(0xF3, 0, (7 << 24) | (3 << 12) | 0x800),
                *(crop_upload if crop else block_upload),
                tile(0, 0, line=1, s=(1, 2, 0), t=(2, 2, 0)), bounds(0, width=4),
                tile(1, 8, fmt=2 if crop == "ci4-odd" else 0,
                     size=0 if crop == "ci4-odd" else 2, line=1, s=(0, 2, 1), t=(0, 2, 0)),
                bounds(1, uls=4, width=4),
                packet(0x06, 0x000204, 0x000406),
                *((packet(0xD8, 0, 64),) if transform else ())))
            archive.writestr(xlu, display_list(
                packet(0xD9, 0, 1), packet(0xEF, 0, 0x00504850),
                packet(0xFC, 0, (4 << 15) | (4 << 9)), ref(0x32, vtx, 0x4008),
                packet(0x05, 0x000204)))
            commands = [(10, bytes((0, 0, 1, 0)) + text(opaque) + text(xlu))]
            if not shared:
                commands.append((3, text(room + "/collision")))
                archive.writestr(room + "/collision", collision([quad], collision_flags))
            archive.writestr(room, scene(*commands))
        if shared:
            archive.writestr("workshop/collision", collision(quads, collision_flags))
        if lighting:
            archive.writestr("workshop/light-directional", resource(
                "OBLB", bytes((128, 128, 128, 0, 128, 128, 128, 0, 0, 0, 129, 0))))
            archive.writestr("workshop/light-ambient", resource(
                "OBLB", bytes((32, 32, 32, 0, 32, 32, 32, 0, 0, 0, 0, 0))))


def export_fixture(work, *, shared=False):
    source = work / "workshop.o2r"
    fixture(source, shared=shared)
    output = work / "install"
    command = [sys.executable, str(Path(__file__).resolve().parents[1] / "tools/o2r_to_wsc3.py"),
               str(source), ROOT, str(output)]
    if shared:
        layout = work / "layout.json"
        layout.write_text(json.dumps({"collision_rooms": [0, 0, 1, 1]}))
        command.extend(("--layout", str(layout)))
    subprocess.run(command, check=True)
    return output


class ImportTest(unittest.TestCase):
    def convert(self, path, layout=None, *, scale=Fraction(1), offset=(0, 0, 0), expanded=False):
        return convert_archive(path, ROOT, layout or {}, scale, offset, expanded, {})

    def test_uniform_world_scale_preserves_materials_and_transforms_bounds(self):
        from tools.o2r_to_wsc3 import Conversion
        transform = Conversion(None, ROOT, Fraction(7, 4), (0, 0, 0), {}, True)
        self.assertEqual(transform.point([-400, 100, 1000]), [-700, 175, 1750])
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "scene.o2r"
            fixture(path)
            from tools import o2r_to_wsc3 as converter
            scenes = []
            encoder = converter.wsc.encode_scene
            def capture(scene):
                scenes.append(scene)
                return encoder(scene)
            with patch.object(converter.wsc, "encode_scene", side_effect=capture):
                original, pngs, before = self.convert(path)
                scaled, other_pngs, after = self.convert(path, scale=Fraction(7, 4))
            for base_room, scaled_room in zip(scenes[0]["rooms"], scenes[1]["rooms"]):
                self.assertEqual(scaled_room["collision_vertices"],
                                 [[round(v * Fraction(7, 4)) for v in point]
                                  for point in base_room["collision_vertices"]])
                for base_batch, scaled_batch in zip(base_room["batches"], scaled_room["batches"]):
                    self.assertEqual(base_batch["material"], scaled_batch["material"])
                    for base_vertex, scaled_vertex in zip(base_batch["render_vertices"], scaled_batch["render_vertices"]):
                        self.assertEqual(scaled_vertex["position"], [round(v * Fraction(7, 4)) for v in base_vertex["position"]])
                        self.assertEqual({k:v for k,v in base_vertex.items() if k != "position"},
                                         {k:v for k,v in scaled_vertex.items() if k != "position"})
            self.assertIsNotNone(scaled)
            self.assertEqual(pngs, other_pngs)
            self.assertEqual(before["materials"], after["materials"])
            self.assertEqual(after["world_transform"]["scale"], "7/4")
            for a, b in zip(before["entrances"], after["entrances"]):
                self.assertEqual(b["yaw"], a["yaw"])
                for axis in (0, 2):
                    self.assertEqual(b["position"][axis], round(a["position"][axis] * Fraction(7, 4)))

    def test_round_trip_is_deterministic_and_preserves_real_packet_order(self):
        with TemporaryDirectory() as temp:
            work = Path(temp)
            output = export_fixture(work)
            package, pngs, report = self.convert(work / "workshop.o2r")
            self.assertEqual((package, pngs, report), self.convert(work / "workshop.o2r"))
            self.assertEqual((output / "scenes/4100.wsc").read_bytes(), package)
            self.assertEqual(package[:14], struct.pack("<4sI3H", b"WSC3", 4100, 2, 2, 0))
            self.assertEqual(report["candidate_draws"], 4)
            self.assertEqual(report["rooms"], [
                {"id": 1, "neighbors": [2], "batches": 2, "render_triangles": 3, "collision_triangles": 2},
                {"id": 2, "neighbors": [1], "batches": 2, "render_triangles": 3, "collision_triangles": 2},
            ])
            self.assertEqual(report["entrances"][0]["position"], [-180, 16, 0])
            self.assertEqual(report["entrances"][1]["yaw"], -0x4000)
            self.assertEqual({p[2][:8] for p in pngs.values()},
                             {bytes.fromhex("ff0000ff00ff00ff"), bytes.fromhex("0000ffff0000ffff")})
            self.assertEqual(len(list(output.glob("gfx/scenes/*.png"))), 2)
            self.assertEqual(report["compatibility"]["draws"],
                             {"candidate": 4, "translated": 4, "rejected": 0, "rejection_reasons": {}})
            self.assertEqual(report["compatibility"]["geometry"]["render_triangles"], 6)
            self.assertEqual(report["compatibility"]["collision"]["triangles"], 4)
            self.assertEqual(report["compatibility"]["rooms"]["neighbor_links"], 1)
            self.assertEqual(report["compatibility"]["memory_estimate"]["package_bytes"], len(package))

    def test_shared_collision_has_explicit_ownership_and_safe_spawns(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "shared.o2r"
            fixture(path, shared=True)
            with self.assertRaisesRegex(ValueError, "collision_rooms"):
                self.convert(path)
            with self.assertRaisesRegex(ValueError, "no collision"):
                self.convert(path, {"collision_rooms": [0, 0, 0, 0]})
            _, _, report = self.convert(path, {"collision_rooms": [0, 0, 1, 1], "entrance": 1},
                                        scale=Fraction(2), offset=(100, 40, 200))
            spawn = next(row for row in report["entrances"] if row["id"] == 0)
            self.assertEqual((spawn["room"], spawn["position"]), (2, [460, 56, 200]))
            with self.assertRaisesRegex(ValueError, "safe local floor"):
                self.convert(path, {"collision_rooms": [0, 0, 1, 1]}, scale=Fraction(1, 10))

    def test_collision_corner_flags_require_explicit_reported_approximation(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "flagged-collision.o2r"
            fixture(path, shared=True, collision_flags=(1, 0))
            ownership = {"collision_rooms": [0, 0, 1, 1]}
            with self.assertRaisesRegex(ValueError, "unsupported source collision corner flags \\[1, 0\\]"):
                self.convert(path, ownership)

            layout = {**ownership, "ignore_collision_vertex_flags": [[1, 0]]}
            package, _, report = self.convert(path, layout)
            self.assertIsNotNone(package, report)
            approximation = next(row for row in report["collision_approximations"]
                                 if row.get("source_corner_flags") == [1, 0])
            self.assertEqual(approximation["polygon_count"], 4)
            self.assertIn("semantics are not translated", approximation["reason"])

            fixture(path, shared=True, collision_flags=(0, 1))
            with self.assertRaisesRegex(ValueError, "unsupported source collision corner flags \\[0, 1\\]"):
                self.convert(path, layout)

    def test_only_explicitly_excluded_unsafe_source_entrances_are_omitted(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "entrances.o2r"
            fixture(path)
            with patch("tools.o2r_to_wsc3.safe_spawn", side_effect=[[0, 16, 0], None]):
                package, _, report = self.convert(path, {"exclude_entrances": [1]})
            self.assertIsNotNone(package, report)
            self.assertEqual([row["id"] for row in report["entrances"]], [0])
            self.assertEqual(report["entrance_approximations"][0]["source_entrance"], 1)
            self.assertIn("no safe local floor", report["entrance_approximations"][0]["reason"])

            with patch("tools.o2r_to_wsc3.safe_spawn", side_effect=[[0, 16, 0], None]):
                with self.assertRaisesRegex(ValueError, "default source entrance 1 is excluded"):
                    self.convert(path, {"exclude_entrances": [1], "entrance": 1})

            with patch("tools.o2r_to_wsc3.safe_spawn", side_effect=[[0, 16, 0], [0, 16, 0]]):
                with self.assertRaisesRegex(ValueError, "has a safe local floor and cannot be excluded"):
                    self.convert(path, {"exclude_entrances": [1]})

            with patch("tools.o2r_to_wsc3.safe_spawn", side_effect=[None, None]):
                with self.assertRaisesRegex(ValueError, "at least one safe entrance is required"):
                    self.convert(path, {"exclude_entrances": [0, 1]})

    def test_fog_lowers_to_wsc4_with_runtime_coefficients(self):
        with TemporaryDirectory() as temp:
            work = Path(temp)
            path = work / "fog.o2r"
            fixture(path, fog=True)
            package, _, report = self.convert(path)
            self.assertIsNotNone(package)
            self.assertEqual(package[:4], b"WSC4")
            self.assertEqual(report["compatibility"]["draws"]["rejected"], 0)
            self.assertEqual(report["rooms"][0]["render_triangles"], 3)
            self.assertIn(b"\x80\x00\x20\x00\xaa\xbb\xcc\xff", package)

    def test_hyrule_texture_edge_with_constant_opaque_alpha_is_exactly_opaque(self):
        draw = {
            "pass": "opaque",
            "geometry": {"mode": 0, "known_mask": 0xFFFFFF},
            "render_state": {
                "reference_blender": {"fog": False, "blend_color": False, "invisible": False,
                                       "coverage_alpha": False, "alpha": True},
                "other_mode_low": 0xC8113078, "other_mode_high": 0,
                "depth_test": True, "depth_write": True, "z_mode": 0,
                "alpha_compare": 0, "blender_raw": 0,
            },
            "combiner": {"effective": {"rgb": "shade.rgb", "alpha": 1,
                                        "inputs": ["shade.rgb"], "texture_inputs": []},
                          "colors": {"primitive": {"rgba": [255, 255, 255, 255]},
                                     "environment": {"rgba": [128, 128, 128, 128]}}},
            "fog": {"enabled": False, "vertex_factors": [], "color": None},
            "textures": [], "approximations": [],
        }
        material, _, alpha_shade = lower_material(draw, None, {}, {})
        self.assertFalse(material["cutout"])
        self.assertEqual(material["primitive"][3], 255)
        self.assertFalse(alpha_shade)

        draw["combiner"]["effective"]["alpha"] = 0
        with self.assertRaisesRegex(ValueError, "not constant opaque"):
            lower_material(draw, None, {}, {})

    def test_dual_texture_primitive_alpha_requires_neutral_extra_alpha_inputs(self):
        draw = {
            "pass": "translucent", "geometry": {"mode": 0, "known_mask": 0xFFFFFF},
            "render_state": {
                "reference_blender": {"fog": False, "blend_color": False, "invisible": False,
                                       "coverage_alpha": False, "alpha": True},
                "other_mode_low": 0xC81049D8, "other_mode_high": 0x80000,
                "depth_test": True, "depth_write": False, "z_mode": 0,
                "alpha_compare": 0, "blender_raw": 0,
            },
            "combiner": {"effective": {"rgb": ("combine", "tex0.rgb", "tex1.rgb",
                                                   "environment.a", "tex1.rgb"),
                                        "alpha": "primitive.a",
                                        "inputs": ["primitive.a", "tex0.rgb", "tex1.rgb"],
                                        "texture_inputs": [0, 1]},
                          "colors": {"primitive": {"rgba": [255, 255, 255, 128]},
                                     "environment": {"rgba": [128, 128, 128, 96]}}},
            "fog": {"enabled": False, "vertex_factors": [], "color": None},
            "textures": [{"id": 0}, {"id": 1}], "approximations": [],
        }
        decoded = {"a": (4, 1, bytes([255, 0, 0, 255] * 4)),
                   "b": (4, 1, bytes([0, 255, 0, 255] * 4))}
        def bind(texture, resources, entries, pngs, filter_mode):
            return {"name": "a" if texture["id"] == 0 else "b"}
        with patch("tools.o2r_to_wsc3.texture_binding", side_effect=bind):
            material, _, _ = lower_material(draw, None, {}, decoded, vertex_alpha_opaque=True)
            self.assertEqual(material["primitive"][3], 128)
            self.assertEqual(material["environment"][3], 96)
            self.assertTrue(alpha_bytes_are_opaque(decoded["a"][2]))
            with self.assertRaisesRegex(ValueError, "vertex alpha"):
                lower_material(draw, None, {}, decoded, vertex_alpha_opaque=False)
            decoded["b"] = (4, 1, bytes([0, 255, 0, 254] * 4))
            with self.assertRaisesRegex(ValueError, "texture alpha"):
                lower_material(draw, None, {}, decoded, vertex_alpha_opaque=True)

    def test_hyrule_decal_material_lowers_only_with_verified_static_inputs(self):
        lod_alpha = ("combine", "tex0.a", 0, "primitive_lod_fraction",
                     ("multiply", "primitive.a", "tex0.a"))
        self.assertEqual(fold_static_primitive_lod_alpha(lod_alpha, 0),
                         ("multiply", "primitive.a", "tex0.a"))
        with self.assertRaisesRegex(ValueError, "nonzero"):
            fold_static_primitive_lod_alpha(lod_alpha, 1)

        draw = {
            "pass": "translucent", "geometry": {"mode": 0, "known_mask": 0xFFFFFF},
            "render_state": {
                "reference_blender": {"fog": True, "blend_color": False, "invisible": False,
                                      "coverage_alpha": False, "alpha": True},
                "other_mode_low": 0xC8104F50, "other_mode_high": 0x80000,
                "depth_test": True, "depth_write": False, "z_mode": 3,
                "alpha_compare": 0, "force_blend": True, "blender_raw": 0xC810,
            },
            "combiner": {"effective": {"rgb": ("multiply", "primitive.rgb",
                                                    ("multiply", "shade.rgb", "tex0.rgb")),
                                        "alpha": lod_alpha,
                                        "inputs": ["primitive.a", "primitive.rgb", "primitive_lod_fraction",
                                                   "shade.rgb", "tex0.a", "tex0.rgb"],
                                        "texture_inputs": [0]},
                          "colors": {"primitive": {"rgba": [250, 220, 140, 200], "lod_fraction": 0},
                                     "environment": {"rgba": [128, 128, 128, 128]}}},
            "fog": {"enabled": True, "vertex_factors": [{"multiplier": 0, "offset_coefficient": 0}],
                    "color": {"rgba": [30, 10, 10, 0]}},
            "textures": [{"id": 0}], "approximations": [],
        }
        opaque = bytes([255, 255, 255, 255] * 4)

        def bind(*_args):
            return {"name": "texture", "width": 4, "height": 1, "address": ["repeat", "repeat"],
                    "scale": [256, 256], "origin": [0, 0], "scroll": [0, 0], "filter": "point"}

        with patch("tools.o2r_to_wsc3.texture_binding", side_effect=bind):
            material, shade_rgb, shade_alpha = lower_material(
                draw, None, {}, {"texture": (4, 1, opaque)}, vertex_alpha_opaque=True)
        self.assertTrue(material["decal"])
        self.assertTrue(material["blend"])
        self.assertFalse(material["depth_write"])
        self.assertEqual(material["primitive"][3], 200)
        self.assertTrue(shade_rgb)
        self.assertFalse(shade_alpha)

        with patch("tools.o2r_to_wsc3.texture_binding", side_effect=bind):
            with self.assertRaisesRegex(ValueError, "vertex alpha"):
                lower_material(draw, None, {}, {"texture": (4, 1, opaque)}, vertex_alpha_opaque=False)

        nonzero = {**draw, "combiner": {**draw["combiner"], "colors": {
            **draw["combiner"]["colors"], "primitive": {"rgba": [250, 220, 140, 200],
                                                           "lod_fraction": 1}}}}
        with patch("tools.o2r_to_wsc3.texture_binding", side_effect=bind):
            with self.assertRaisesRegex(ValueError, "LOD interpolation is nonzero"):
                lower_material(nonzero, None, {}, {"texture": (4, 1, opaque)}, vertex_alpha_opaque=True)

    def test_static_fast3d_lighting_is_evaluated_into_wsc3_shade(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "lighting.o2r"
            fixture(path, lighting=True)
            package, _, report = self.convert(path)
            self.assertIsNotNone(package)
            self.assertEqual(package[:4], b"WSC3")
            self.assertEqual(report["compatibility"]["draws"]["rejected"], 0)
            self.assertIn(bytes((128, 128, 128, 255)), package)
            self.assertNotIn(bytes((200, 180, 160, 128)), package[:200])

    def test_explicit_pass_prelude_and_unresolved_segment(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "prelude.o2r"
            fixture(path)
            with ZipFile(path) as archive:
                entries = {name: archive.read(name) for name in archive.namelist()}
            from tools.o2r_draw_trace import Reference

            opaque = ROOMS[0] + "/opa"
            prelude = "workshop/shared-state"
            original = entries[opaque]
            # Header (64 bytes) and ODLT alignment (8) precede two state packets.
            begin, length = 72, 16
            entries[prelude] = display_list(original[begin:begin + length])
            entries[opaque] = original[:begin] + original[begin + length:]
            with ZipFile(path, "w") as archive:
                for name, value in entries.items():
                    archive.writestr(name, value)
            self.assertIsNone(self.convert(path)[0])
            self.assertIsNotNone(self.convert(path, {"pass_preludes": {"opaque": prelude}})[0])
            entries[opaque] = original[:begin] + packet(0xDE, 0, 0x08000001) + original[begin + length:]
            with ZipFile(path, "w") as archive:
                for name, value in entries.items():
                    archive.writestr(name, value)
            result = self.convert(path)
            self.assertIsNone(result[0])
            self.assertIn("unbound segment 0x08", result[2]["trace_issues"][0]["reason"])
            self.assertIsNotNone(convert_archive(path, ROOT, {}, Fraction(1), (0, 0, 0),
                                                 False, {8: Reference(prelude)})[0])

    def dry_run(self, path, scene_name=ROOT, *args):
        converter = Path(__file__).resolve().parents[1] / "tools/o2r_to_wsc3.py"
        result = subprocess.run([sys.executable, str(converter), str(path), scene_name, "--dry-run", *args],
                                capture_output=True, text=True)
        return result.returncode, json.loads(result.stdout)

    def test_dry_run_is_deterministic_and_does_not_stage_any_files(self):
        with TemporaryDirectory() as temp:
            work = Path(temp)
            path = work / "workshop.o2r"
            fixture(path)
            first = self.dry_run(path)
            self.assertEqual(first, self.dry_run(path))
            code, report = first
            self.assertEqual((code, report["status"], report["texture_count"]), (0, "compatible", 2))
            self.assertEqual(report["compatibility"]["draws"]["translated"], 4)
            self.assertIn("CI4", report["compatibility"]["textures"]["formats"])
            self.assertIn("workshop/palette", report["resources"]["converted"])
            self.assertIn("workshop/east/collision", report["resources"]["converted"])
            self.assertEqual(len(report["entrances"]), 2)
            self.assertEqual(list(work.iterdir()), [path])

    def test_rejected_dry_run_reports_geometry_and_still_checks_collision(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "broken.o2r"
            fixture(path, fog=True, incomplete_fog=True)
            code, report = self.dry_run(path)
            self.assertEqual((code, report["status"]), (2, "rejected"))
            self.assertIn("fog", report["draw_issues"][0]["reason"])
            self.assertEqual(report["draw_issues"][0]["display_list"], ROOMS[0] + "/opa")
            self.assertEqual([room["collision_triangles"] for room in report["rooms"]], [2, 2])
            self.assertEqual(len(report["entrances"]), 2)
            code, report = self.dry_run(path, "workshop/missing")
            self.assertEqual(code, 2)
            self.assertEqual(report["resources"]["observed"], [{"path": "workshop/missing", "status": "missing"}])

    def test_loadtile_bakes_aligned_subrect_with_exact_source_rows(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "crop.o2r"
            fixture(path, crop="valid")
            package, pngs, report = self.convert(path)
            self.assertIsNotNone(package, report)
            wanted = bytes.fromhex("0000ffff" * 12 + "00ff00ff" * 4)
            self.assertIn((4, 4, wanted), pngs.values())
            fixture(path, crop="wrong-line")
            package, _, report = self.convert(path)
            self.assertIsNone(package)
            self.assertIn("TMEM stride", report["draw_issues"][0]["reason"])

    def test_loadtile_bakes_ci4_crop_from_odd_nibble_origin(self):
        source = bytes.fromhex("0123456789abcdef")
        self.assertEqual(crop_ci4(source, 8, 2, 1, 0, 4, 2), bytes.fromhex("12349abc"))
        with TemporaryDirectory() as temp:
            path = Path(temp) / "ci4-crop.o2r"
            fixture(path, crop="ci4-odd")
            package, pngs, report = self.convert(path)
            self.assertIsNotNone(package, report)
            # Source crop is x=1..4, y=1..4 from an 8x6 packed CI4 image.
            cropped_indices = [(x + 2 * y) % 4 for y in range(1, 5) for x in range(1, 5)]
            expected = bytes.fromhex("f80107c1003fffff")
            palette_rgba = []
            for i in range(0, len(expected), 2):
                packed = expected[i] << 8 | expected[i + 1]
                rgb = [((packed >> shift) & 31) * 255 // 31 for shift in (11, 6, 1)]
                palette_rgba.extend((*rgb, 255 if packed & 1 else 0))
            expanded = bytes(channel for index in cropped_indices
                             for channel in palette_rgba[index * 4:index * 4 + 4])
            self.assertIn((4, 4, expanded), pngs.values())
            self.assertEqual(report["status"], "converted")

    def test_static_f3dex2_matrix_is_baked_and_reported(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "matrix.o2r"
            fixture(path, transform=True)
            package, _, report = self.convert(path)
            self.assertIsNotNone(package, report)
            self.assertIn("workshop/translate", report["resources"]["converted"])
            self.assertEqual(report["trace_coverage"]["static_matrix_resources"], ["workshop/translate"])
            traced = trace.trace_archive(path, ROOT)
            expected_matrix = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                               [0.0, 0.0, 1.0, 0.0], [100.0, 0.0, 0.0, 1.0]]
            self.assertEqual(traced["draws"][0]["geometry"]["vertex_ranges"][0]
                             ["modelview_at_vertex_load"], expected_matrix)
            self.assertEqual(transform_position([2, 3, 4], expected_matrix), [102.0, 3.0, 4.0])
            package_again, _, report_again = self.convert(path)
            self.assertEqual(package_again, package)
            self.assertEqual(report_again, report)
            identity_path = Path(temp) / "identity.o2r"
            fixture(identity_path)
            identity_package, _, _ = self.convert(identity_path)
            self.assertNotEqual(package, identity_package)

    def test_static_segmented_matrix_uses_explicit_segment_binding(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "matrix-segment.o2r"
            fixture(path, transform="segmented")
            segment = {8: trace.Reference("workshop/translate")}
            package, _, report = convert_archive(path, ROOT, {}, Fraction(1), (0, 0, 0), False, segment)
            self.assertIsNotNone(package, report)
            self.assertIn("workshop/translate", report["resources"]["converted"])

    def test_per_room_segment_bases_resolve_shared_address_and_validate_offset(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "scene.o2r"
            fixture(path)
            with ZipFile(path) as archive:
                entries = {name: archive.read(name) for name in archive.namelist()}
            bindings = {}
            for path_prefix in ROOMS:
                path_name = path_prefix + "/opa"
                original = entries[path_name]
                # Packets are relative to the 8-byte ODLT command start.
                helper = path_prefix + "/state"
                entries[helper] = display_list(packet(0), original[72:88])
                entries[path_name] = original[:72] + packet(0xDE, 0, 0x08000001) + original[88:]
                bindings[path_prefix] = ["08@8=" + helper]
            with ZipFile(path, "w") as archive:
                for name, data in entries.items():
                    archive.writestr(name, data)
            package, _, report = self.convert(path, {"room_segments": bindings})
            self.assertIsNotNone(package, report)
            self.assertIn(ROOMS[1] + "/state", report["trace_coverage"]["decoded_display_lists"])
            bindings[ROOMS[1]][0] = "08@2=" + ROOMS[1] + "/state"
            package, _, report = self.convert(path, {"room_segments": bindings})
            self.assertIsNone(package)
            self.assertIn("packet boundary", report["trace_issues"][0]["reason"])

    def test_tile_format_disagreement_and_world_grid_overflow_are_rejected(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "malformed.o2r"
            fixture(path, format_error=True)
            package, _, report = self.convert(path)
            self.assertIsNone(package)
            self.assertIn("tile format disagrees", report["draw_issues"][0]["reason"])
            fixture(path)
            with self.assertRaisesRegex(ValueError, "outside collision grid"):
                self.convert(path, offset=(8200, 0, 0))
            self.assertIsNotNone(self.convert(path, offset=(8200, 0, 0), expanded=True)[0])


if __name__ == "__main__":
    unittest.main()
