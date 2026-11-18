#!/usr/bin/env python3
"""Exercise archive inventory and transition evidence with original fixtures."""

from pathlib import Path
import json
import struct
import sys
from tempfile import TemporaryDirectory
import unittest
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.hyrule_importer import ImportFailure, crc64_resource_path
from tools.o2r_inventory import inventory_archive, main


def resource(kind, body, version=0):
    return bytes((1, 0, 0, 0)) + struct.pack(">II", kind, version) + bytes(52) + body


def string(value):
    data = value.encode("utf-8")
    return struct.pack(">I", len(data)) + data


def scene(*commands):
    return resource(0x4F524F4D, struct.pack(">I", len(commands))
                    + b"".join(struct.pack(">i", opcode) + payload for opcode, payload in commands))


def display_list(*instructions):
    body = bytes((4,)) + bytes(7) + b"".join(instructions) + struct.pack(">II", 0xDF000000, 0)
    return resource(0x4F444C54, body)


def hash_ref(opcode, path):
    return struct.pack(">IIQ", opcode << 24, 0, crc64_resource_path(path))


class O2RInventoryTests(unittest.TestCase):
    def test_scene_and_room_links_follow_archive_evidence(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.o2r"
            prefix = "scenes/shared/studio"
            root = prefix + "/studio"
            alternate = prefix + "/studioSet_2"
            left, right = prefix + "/left", prefix + "/right"
            mesh, texture = prefix + "/entry-dl", "shared/pixel"
            missing, wrong_type = "shared/missing", "shared/not-a-texture"
            room_list = struct.pack(">I", 2) + string(left) + bytes(8) + string(right) + bytes(8)
            valid = struct.pack(">4Bh4hH", 0, 1, 1, 2, 0, 0, 0, 0, 0, 0)
            invalid = struct.pack(">4Bh4hH", 0, 0, 255, 0, 0, 0, 0, 0, 0, 0)
            actor = struct.pack(">I8H", 1, 0, 10, 20, 30, 0, 0, 0, 0)
            with ZipFile(path, "w") as bundle:
                bundle.writestr(root, scene(
                    (4, room_list),
                    (14, struct.pack(">I", 2) + valid + invalid),
                    (6, struct.pack(">Ibb", 1, 0, 1)),
                    (0, actor),
                    (19, struct.pack(">IH", 1, 616)),
                    (24, struct.pack(">I", 1) + string(alternate)),
                    (20, b""),
                ))
                bundle.writestr(alternate, scene((14, struct.pack(">I", 1) + valid), (20, b"")))
                bundle.writestr(left, scene((10, bytes((0, 0, 1, 0)) + string(mesh) + string("")), (20, b"")))
                bundle.writestr(right, scene((20, b"")))
                bundle.writestr(mesh, display_list(
                    hash_ref(0x20, texture), hash_ref(0x20, missing), hash_ref(0x20, wrong_type)))
                bundle.writestr(texture, resource(0x4F544558, struct.pack(">IIIIH", 2, 1, 1, 2, 0xFFFE)))
                bundle.writestr(wrong_type, resource(0x4F504154, b"unsupported"))
            before = path.read_bytes()

            report = inventory_archive(path, verify_crc=True)

            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(list(Path(directory).iterdir()), [path])
            self.assertTrue(report["zip_crc_verified"])
            self.assertEqual(report["totals"]["scenes"], 1)
            self.assertEqual(report["totals"]["room_headers"], 2)
            self.assertEqual(report["totals"]["alternate_headers"], 1)
            indexed_scene, = report["scenes"]
            self.assertEqual(indexed_scene["path"], root)
            self.assertEqual(indexed_scene["rooms"], [left, right])
            self.assertEqual(indexed_scene["spawn_count"], 1)
            self.assertEqual(indexed_scene["entrances"], [{"spawn": 0, "room": 1}])
            self.assertEqual(indexed_scene["coverage"]["resource_counts"],
                             {"DisplayList": 1, "Scene": 4, "Texture": 1})
            graph = report["transition_graph"]
            self.assertEqual([(edge["header"], edge["from_room"], edge["to_room"])
                              for edge in graph["room_links"]],
                             [(root, left, right), (alternate, left, right)])
            self.assertEqual(graph["unresolved_room_links"][0]["back_room"], 255)
            self.assertEqual(graph["unresolved_cross_scene_exits"][0]["entrance_id"], 616)
            self.assertNotIn("to_room", graph["unresolved_cross_scene_exits"][0])
            listing = report["resources"][mesh]
            self.assertEqual(listing["status"], "partial")
            self.assertEqual(listing["dependencies"], [texture])
            self.assertEqual({entry["hash"] for entry in listing["unresolved_hashes"]},
                             {f"0x{crc64_resource_path(ref):016x}" for ref in (missing, wrong_type)})
            self.assertEqual(report["resources"][texture]["texture"]["width"], 1)

    def test_failed_header_does_not_hide_other_scenes(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.o2r"
            root, room = "scenes/shared/first/first", "scenes/shared/first/room"
            missing_room = "scenes/shared/first/missing"
            other = "scenes/shared/second/second"
            room_list = struct.pack(">I", 2) + string(room) + bytes(8) + string(missing_room) + bytes(8)
            with ZipFile(path, "w") as bundle:
                bundle.writestr(root, scene((4, room_list), (20, b"")))
                bundle.writestr(room, resource(0x4F524F4D, struct.pack(">Ii", 1, 4)))
                bundle.writestr(other, resource(0x4F524F4D, bytes(4), version=8))

            report = inventory_archive(path)

            self.assertEqual(report["totals"]["scenes"], 2)
            self.assertEqual(report["resources"][room]["status"], "invalid")
            self.assertEqual(report["resources"][other]["status"], "unsupported")
            self.assertEqual(report["scenes"][0]["coverage"]["missing_paths"], [missing_room])

    def test_unsafe_zip_path_is_rejected(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.o2r"
            with ZipFile(path, "w") as bundle:
                bundle.writestr("../escape", b"data")
            with self.assertRaisesRegex(ImportFailure, "unsafe archive path"):
                inventory_archive(path)

    def test_versioned_catalog_output_is_deterministic(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            archive_path = root / "synthetic.o2r"
            output_path = root / "catalog.json"
            scene_path = "scenes/shared/sample/sample"
            with ZipFile(archive_path, "w") as bundle:
                bundle.writestr(scene_path, scene((20, b"")))
            self.assertEqual(main([str(archive_path), "--output", str(output_path)]), 0)
            first = output_path.read_bytes()
            self.assertEqual(main([str(archive_path), "--output", str(output_path)]), 0)
            self.assertEqual(output_path.read_bytes(), first)
            report = json.loads(first)
            self.assertEqual(report["format"], "o2r-scene-inventory")
            self.assertEqual(report["version"], 1)
            self.assertEqual(report["scenes"][0]["path"], scene_path)


if __name__ == "__main__":
    unittest.main()
