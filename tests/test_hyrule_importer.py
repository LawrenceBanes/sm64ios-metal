import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.hyrule_importer import (ImportFailure, collision_floor_at, collision_geometry_diagnostics,
                                  import_archive, parse_collision, parse_scene,
                                  coordinate, read_resource_header, safe_archive_path)


def resource_header(resource_type, version=0):
    return bytes([1, 0, 0, 0]) + struct.pack(">IIQ", resource_type, version, 0x123456789ABCDEF0) + bytes(44)


def collision_body(vertices=None, indices=(0, 1, 2)):
    vertices = vertices or [(0, 0, 0), (100, 0, 0), (0, 0, 100)]
    out = bytearray(struct.pack(">6hi", -10, -1, -10, 10, 20, 10, len(vertices)))
    for vertex in vertices:
        out.extend(struct.pack(">3h", *vertex))
    out.extend(struct.pack(">I", 1))
    out.extend(struct.pack(">7H", 2, indices[0], indices[1], indices[2], 0, 32767, 0))
    out.extend(struct.pack(">h", 0))
    out.extend(struct.pack(">I2I", 1, 0x55667788, 0x11223344))
    out.extend(struct.pack(">IiI", 0, 0, 0))
    return bytes(out)


def scene_body(collision_path="scenes/shared/spot00_scene/CollisionHeader"):
    encoded_path = collision_path.encode("utf-8")
    return (struct.pack(">I", 2) + struct.pack(">iI", 3, len(encoded_path)) + encoded_path
            + struct.pack(">i", 20))


class HyruleImporterTests(unittest.TestCase):
    def test_resource_header_reads_o2r_byte_order_and_fourcc(self):
        data = resource_header(0x4F434F4C) + b"payload"
        header = read_resource_header(data, "CollisionHeader")
        self.assertEqual(header["status"], "ok")
        self.assertEqual(header["endian"], "big")
        self.assertEqual(header["resource_type"], "CollisionHeader")
        self.assertEqual(header["body_offset"], 64)

    def test_collision_header_decodes_triangle_and_surface_words(self):
        collision = parse_collision(collision_body(), ">", "fixture/CollisionHeader")
        self.assertEqual(collision["vertices"][1], [100, 0, 0])
        self.assertEqual(collision["polygons"][0]["vertex_indices"], [0, 1, 2])
        self.assertEqual(collision["surface_types"], [[0x11223344, 0x55667788]])

    def test_collision_rejects_out_of_range_vertex_reference(self):
        with self.assertRaisesRegex(ImportFailure, "missing vertex"):
            parse_collision(collision_body(indices=(0, 1, 3)), ">", "fixture/CollisionHeader")

    def test_scene_records_collision_dependency_and_end_marker(self):
        scene = parse_scene(scene_body(), ">", "fixture/Scene")
        self.assertTrue(scene["fully_decoded"])
        self.assertEqual(scene["dependencies"], ["scenes/shared/spot00_scene/CollisionHeader"])
        self.assertEqual([command["id"] for command in scene["commands"]], [3, 20])

    def test_scene_cutscene_command_records_its_resource_dependency(self):
        cutscene = "scenes/shared/spot00_scene/Cutscene"
        encoded = cutscene.encode("utf-8")
        body = struct.pack(">IiI", 2, 23, len(encoded)) + encoded + struct.pack(">i", 20)
        parsed = parse_scene(body, ">", "fixture/Scene")
        self.assertTrue(parsed["fully_decoded"])
        self.assertEqual(parsed["dependencies"], [cutscene])

    def test_unknown_payload_stops_with_precise_diagnostic(self):
        body = struct.pack(">Iii", 2, 3, 1) + b"x" + struct.pack(">i", 2)
        parsed = parse_scene(body, ">", "fixture/Scene")
        self.assertFalse(parsed["fully_decoded"])
        self.assertEqual(parsed["unsupported_at"]["name"], "SetCsCamera")
        self.assertEqual(parsed["dependencies"], ["x"])

    def test_zip_import_writes_versioned_manifest_and_collision_obj(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive_path = root / "oot.o2r"
            prefix = "scenes/shared/spot00_scene"
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr(prefix + "/spot00_scene", resource_header(0x4F524F4D) + scene_body())
                archive.writestr(prefix + "/spot00_sceneSet_0001",
                                 resource_header(0x4F524F4D) + scene_body(prefix + "/MissingAlternateCollision"))
                archive.writestr(prefix + "/CollisionHeader", resource_header(0x4F434F4C) + collision_body())
                archive.writestr("objects/object_spot00_objects/Texture", b"fixture only")
                archive.writestr(prefix + "/source-info.bin", b"headerless fixture")
            output = root / "converted"
            result = import_archive(archive_path, output, prefix, 1.5)
            manifest = json.loads((output / "hyrule-world.json").read_text())
            self.assertEqual(manifest["format_version"], 1)
            self.assertEqual(manifest["archive"]["entry_count"], 5)
            self.assertEqual(result["region"]["scene_resource"], prefix + "/spot00_scene")
            self.assertEqual(result["collision_outputs"][0]["triangles"], 1)
            self.assertEqual(result["decoded_resource_count"], 3)
            self.assertEqual(result["resource_type_counts"]["headerless-or-short"], 1)
            self.assertEqual(result["dependencies"][0]["status"], "available")
            self.assertEqual(result["region_bounds"], {"min": [-15.0, -1.5, -15.0],
                                                         "max": [15.0, 30.0, 15.0],
                                                         "source": "collision headers"})
            self.assertEqual([entry["path"] for entry in result["resource_index"]],
                             sorted(entry["path"] for entry in result["resource_index"]))
            self.assertIn("v 150.000000 0.000000 0.000000", (output / "spot00.collision.obj").read_text())

    def test_missing_scene_dependency_is_reported_without_fabrication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive_path = root / "oot.o2r"
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("scenes/shared/spot00_scene/Scene",
                                 resource_header(0x4F524F4D) + scene_body("scenes/shared/spot00_scene/MissingCollision"))
            result = import_archive(archive_path, root / "converted", "scenes/shared/spot00_scene", 1.0)
            self.assertEqual(result["dependencies"], [{"path": "scenes/shared/spot00_scene/MissingCollision",
                                                        "status": "missing"}])
            self.assertEqual(result["collision_outputs"], [])
            self.assertIsNone(result["region_bounds"])

    def test_missing_archive_is_a_clear_error(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ImportFailure, "O2R archive not found"):
                import_archive(Path(directory) / "oot.o2r", Path(directory) / "out",
                               "scenes/shared/spot00_scene", 1.0)

    def test_archive_paths_reject_traversal_and_absolute_paths(self):
        for path in ("../escape", "/absolute/path", "folder\\escape"):
            with self.subTest(path=path), self.assertRaises(ImportFailure):
                safe_archive_path(path)

    def test_region_registry_has_named_field_to_ranch_edge(self):
        registry_path = Path(__file__).resolve().parents[1] / "ios/hyrule_regions.json"
        registry = json.loads(registry_path.read_text())
        by_id = {region["id"]: region for region in registry["regions"]}
        field = by_id["hyrule_field"]
        edge = next(edge for edge in field["planned_connections"] if edge["region"] == "lon_lon_ranch")
        self.assertEqual(edge["status"], "pending_source_coordinates")
        self.assertEqual(by_id["lon_lon_ranch"]["source_scene"], "scenes/shared/spot20_scene")
        self.assertEqual(by_id["kakariko_village"]["source_scene"], "scenes/shared/spot01_scene")
        self.assertEqual(by_id["zoras_river"]["source_scene"], "scenes/shared/spot03_scene")
        self.assertEqual(len(by_id), len(registry["regions"]))

    def test_coordinate_scale_preserves_source_axis_order(self):
        self.assertEqual(coordinate([2, -3, 4], 0.25), [0.5, -0.75, 1.0])

    def test_spawn_floor_check_uses_upward_triangle_and_bounds(self):
        collision = parse_collision(collision_body(vertices=[(0, 0, 0), (0, 0, 10), (10, 0, 0)]),
                                    ">", "fixture/CollisionHeader")
        floor = collision_floor_at(collision, 25, 5, 25)
        self.assertIsNone(floor)
        floor = collision_floor_at(collision, 2.5, 5, 2.5)
        self.assertEqual(floor["triangle"], 0)
        self.assertEqual(floor["floor_y"], 0.0)
        self.assertEqual(floor["spawn_y_delta"], 5.0)
        self.assertIsNone(collision_floor_at(collision, 200, 5, 25))

    def test_collision_diagnostics_detect_valid_winding_and_bounds(self):
        collision = parse_collision(collision_body(vertices=[(0, 0, 0), (0, 0, 10), (10, 0, 0)]),
                                    ">", "fixture/CollisionHeader")
        diagnostics = collision_geometry_diagnostics(collision)
        self.assertEqual(diagnostics["source_normal_aligned_triangles"], 1)
        self.assertEqual(diagnostics["reversed_winding_triangles"], 0)
        self.assertEqual(diagnostics["degenerate_triangles"], 0)
        self.assertTrue(diagnostics["declared_bounds_contain_vertices"])


if __name__ == "__main__":
    unittest.main()
