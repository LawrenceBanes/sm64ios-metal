"""Generate original O2R resources and exercise the production converter."""
from fractions import Fraction
import json
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from zipfile import ZipFile

from test_o2r_draw_trace import bounds, display_list, packet, ref, resource, scene, text, tile
from tools.o2r_to_wsc3 import convert_archive

ROOT = "workshop/scene"
ROOMS = ("workshop/west", "workshop/east")


def texture(fmt, width, height, data):
    return resource("OTEX", struct.pack(">4I", fmt, width, height, len(data)) + data)


def floor_points(a, b, y=0):
    return [(a, y, -240), (a, y, 240), (b, y, 240), (b, y, -240)]


def collision(polygons):
    points, indices = [], []
    for quad in polygons:
        base = len(points)
        points.extend(quad)
        indices.extend((base, base + n, base + n + 1) for n in (1, 2))
    low = [min(coordinate) for coordinate in zip(*points)]
    high = [max(coordinate) for coordinate in zip(*points)]
    vertices = b"".join(struct.pack(">3h", *point) for point in points)
    faces = b"".join(struct.pack(">4H4h", 0, *tri, 0, 32767, 0, 0) for tri in indices)
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


def fixture(path, *, shared=False, fog=False, format_error=False):
    with ZipFile(path, "w") as archive:
        archive.writestr(ROOT, header(shared))
        archive.writestr("workshop/rug", texture(3, 4, 4, bytes.fromhex("0101010101010101")))
        archive.writestr("workshop/palette", texture(2, 16, 1, bytes.fromhex("f80107c1") + bytes(28)))
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
                ref(0x32, vtx, 0x4008),
                ref(0x20, "workshop/palette", (2 << 19) | 15), tile(7, 256, fmt=0, size=2),
                packet(0xF0, 0, (7 << 24) | (15 << 14)),
                ref(0x20, "workshop/rug", 2 << 19), tile(7, 0, size=2),
                packet(0xF3, 0, (7 << 24) | (3 << 12) | 0x800),
                ref(0x20, "workshop/inlay", (2 << 19) | 3), tile(7, 8, fmt=0, size=2),
                packet(0xF3, 0, (7 << 24) | (15 << 12) | 0x800),
                tile(0, 0, line=1, s=(1, 2, 0), t=(2, 2, 0)), bounds(0, width=4),
                tile(1, 8, fmt=0, size=2, line=1, s=(0, 2, 1), t=(0, 2, 0)),
                bounds(1, uls=4, width=4),
                packet(0x06, 0x000204, 0x000406)))
            archive.writestr(xlu, display_list(
                packet(0xD9, 0, 1), packet(0xEF, 0, 0x00504850),
                packet(0xFC, 0, (4 << 15) | (4 << 9)), ref(0x32, vtx, 0x4008),
                packet(0x05, 0x000204)))
            commands = [(10, bytes((0, 0, 1, 0)) + text(opaque) + text(xlu))]
            if not shared:
                commands.append((3, text(room + "/collision")))
                archive.writestr(room + "/collision", collision([quad]))
            archive.writestr(room, scene(*commands))
        if shared:
            archive.writestr("workshop/collision", collision(quads))


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

    def test_unsupported_draw_fails_with_packet_provenance_and_no_export(self):
        with TemporaryDirectory() as temp:
            work = Path(temp)
            path = work / "unsupported.o2r"
            fixture(path, fog=True)
            package, _, report = self.convert(path)
            self.assertIsNone(package)
            self.assertIn("fog", report["draw_issues"][0]["reason"])
            self.assertEqual(report["draw_issues"][0]["display_list"], ROOMS[0] + "/opa")
            self.assertIsInstance(report["draw_issues"][0]["offset"], int)
            result = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "tools/o2r_to_wsc3.py"),
                                     str(path), ROOT, str(work / "no-output")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("fog", result.stderr)
            self.assertFalse((work / "no-output").exists())

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

    def test_tile_format_disagreement_and_world_grid_overflow_are_rejected(self):
        with TemporaryDirectory() as temp:
            path = Path(temp) / "malformed.o2r"
            fixture(path, format_error=True)
            package, _, report = self.convert(path)
            self.assertIsNone(package)
            self.assertIn("tile format disagrees", report["draw_issues"][0]["reason"])
            fixture(path)
            self.assertIsNone(self.convert(path, offset=(8200, 0, 0))[0])
            self.assertIsNotNone(self.convert(path, offset=(8200, 0, 0), expanded=True)[0])


if __name__ == "__main__":
    unittest.main()
