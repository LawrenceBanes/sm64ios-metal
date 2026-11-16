import json
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.hyrule_importer import (ImportFailure, collision_floor_at, collision_geometry_diagnostics,
                                  import_archive, parse_collision, parse_scene,
                                  coordinate, parse_texture_descriptor, parse_vertex_array,
                                  read_resource_header, safe_archive_path, crc64_resource_path,
                                  parse_display_list, decode_texture_rgba, decode_room_render_data,
                                  emit_native_level_include, oot_angle_to_sm64_degrees,
                                  choose_hyrule_auxiliary_spawns, transform_world_point)


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


def vertex_array_body():
    return (struct.pack(">II", 25, 1)
            + struct.pack(">3hH2h4B", 12, -34, 56, 0, 320, -640, 255, 128, 64, 255))


def f3dex2_command(opcode, w0=0, w1=0):
    return struct.pack(">II", (opcode << 24) | (w0 & 0xFFFFFF), w1 & 0xFFFFFFFF)


def f3dex2_wide(opcode, w0, w1, resource_hash):
    return (f3dex2_command(opcode, w0, w1)
            + struct.pack(">Q", resource_hash))


def vertex_array_with_three_vertices():
    out = bytearray(struct.pack(">II", 25, 3))
    for position, uv in (((0, 0, 0), (0, 0)), ((10, 0, 0), (320, 0)),
                         ((0, 10, 0), (0, 320))):
        out.extend(struct.pack(">3hH2h4B", *position, 0, *uv, 255, 255, 255, 255))
    return bytes(out)


def display_list_body(instructions):
    return bytes([4]) + bytes(7) + b"".join(instructions)


class HyruleImporterTests(unittest.TestCase):
    def test_hyrule_level_is_registered_on_the_existing_sm64_gameplay_path(self):
        root = Path(__file__).resolve().parents[1]
        script = (root / "levels/hyrule_field/script.c").read_text()
        definitions = (root / "levels/level_defines.h").read_text()
        leveldata = (root / "levels/hyrule_field/leveldata.c").read_text()
        makefile = (root / "Makefile").read_text()
        split_makefile = (root / "Makefile.split").read_text()
        self.assertIn("LEVEL_HYRULE_FIELD", definitions)
        self.assertIn("MARIO(/*model*/ MODEL_MARIO", script)
        self.assertIn("TERRAIN(hyrule_field_collision)", script)
        self.assertIn("CALL_LOOP(/*arg*/ 1, /*func*/ lvl_init_or_update)", script)
        self.assertIn("GEO_DISPLAY_LIST(LAYER_OPAQUE, hyrule_field_opaque_dl)",
                      (root / "levels/hyrule_field/geo.c").read_text())
        self.assertIn('"build/hyrule_field_generated.inc.c"', leveldata)
        self.assertNotIn("hyrule_update_player", script)
        self.assertNotIn("hyrule_physics_step", script)
        self.assertIn("IOS_HYRULE_WORLD ?= 0", makefile)
        self.assertIn(".ios_hyrule_mode", makefile)
        self.assertIn(".ios_hyrule_mode", split_makefile)
        self.assertNotIn('STUB_LEVEL(  "",               LEVEL_HYRULE_FIELD', definitions)

    def test_oot_binary_yaw_is_converted_to_mario_pos_degrees(self):
        self.assertEqual(oot_angle_to_sm64_degrees(-3641), -20)
        self.assertEqual(oot_angle_to_sm64_degrees(0x4000), 90)
        self.assertEqual(oot_angle_to_sm64_degrees(-0x8000), -180)
        self.assertEqual(oot_angle_to_sm64_degrees(0), 0)

    def test_auxiliary_kart_and_return_positions_require_flat_collision(self):
        collision = {
            "vertices": [(-2000, 0, -2000), (2000, 0, -2000),
                         (2000, 0, 2000), (-2000, 0, 2000)],
            "polygons": [
                {"vertex_indices": [0, 1, 2], "normal": [0, 32767, 0], "surface_type": 0},
                {"vertex_indices": [0, 2, 3], "normal": [0, 32767, 0], "surface_type": 0},
            ],
        }
        kart, return_warp = choose_hyrule_auxiliary_spawns(collision, [0, 0, 0])
        self.assertEqual(kart, [-500, 0, 0])
        self.assertEqual(return_warp, [800, 0, 0])
        self.assertGreaterEqual(abs(return_warp[0]), 800)

    def test_resource_path_hash_matches_o2r_reference(self):
        self.assertEqual(crc64_resource_path("scenes/shared/spot00_scene/spot00_room_0Vtx_002340"),
                         0x98B2E95F16A5E274)
        self.assertNotEqual(crc64_resource_path("scenes/spot00/é"),
                            crc64_resource_path("scenes/spot00/e"))

    def test_display_list_decodes_wide_references_triangles_and_texture_enable(self):
        path = "scenes/fixture/mesh"
        body = display_list_body([
            f3dex2_wide(0x33, 0, 0, crc64_resource_path(path)),
            f3dex2_wide(0x32, (3 << 12) | (3 << 1), 0, 0x0102030405060708),
            f3dex2_wide(0x20, (2 << 19) | 1, 0, 0x1112131415161718),
            f3dex2_command(0xD7, 2, (0xFFFF << 16) | 0xFFFF),
            f3dex2_command(0x05, (0 << 16) | (2 << 8) | 4),
            f3dex2_command(0xDF),
        ])
        parsed = parse_display_list(body, ">", path)
        self.assertTrue(parsed["terminated"])
        self.assertEqual(parsed["triangle_count"], 1)
        self.assertEqual(parsed["instructions"][1]["vertex_count"], 3)
        self.assertEqual(parsed["instructions"][1]["vertex_destination"], 0)
        self.assertEqual(parsed["instructions"][4]["triangles"], [[0, 1, 2]])
        self.assertTrue(parsed["instructions"][3]["texture_state"]["enabled"])
        self.assertFalse(parse_display_list(display_list_body([
            f3dex2_command(0xD7, 0, 0), f3dex2_command(0xDF)]), ">", path)
            ["instructions"][0]["texture_state"]["enabled"])

    def test_display_list_rejects_missing_end_and_truncated_wide_command(self):
        with self.assertRaisesRegex(ImportFailure, "no end opcode"):
            parse_display_list(display_list_body([f3dex2_command(0x00)]), ">", "fixture/list")
        with self.assertRaisesRegex(ImportFailure, "truncated"):
            parse_display_list(bytes([4]) + bytes(7) + f3dex2_command(0x20), ">", "fixture/list")

    def test_texture_decoder_expands_rgba16_and_packed_intensity_alpha(self):
        rgba16 = {"width": 1, "height": 1, "format_code": 2, "image_data_size": 2}
        self.assertEqual(decode_texture_rgba(rgba16, b"\xf8\x01"), bytes((255, 0, 0, 255)))
        ia4 = {"width": 2, "height": 1, "format_code": 7, "image_data_size": 1}
        self.assertEqual(decode_texture_rgba(ia4, b"\xf1"),
                         bytes((255, 255, 255, 255, 0, 0, 0, 255)))

    def test_texture_decoder_uses_rgba16_palette_for_ci4(self):
        ci4 = {"width": 2, "height": 1, "format_code": 3, "image_data_size": 1}
        palette = [[0, 0, 0, 0], [0, 255, 0, 255]]
        self.assertEqual(decode_texture_rgba(ci4, b"\x10", palette),
                         bytes((0, 255, 0, 255, 0, 0, 0, 0)))

    def test_room_decoder_resolves_vertices_texture_state_and_polygon_translation(self):
        display_path = "scenes/fixture/DisplayList"
        array_path = "scenes/fixture/VertexArray"
        texture_path = "scenes/fixture/Texture"
        palette_path = "scenes/fixture/TLUT"
        array = parse_vertex_array(vertex_array_with_three_vertices(), ">", array_path)
        texture_descriptor = {"format_code": 3, "format": "Palette4bpp", "width": 2,
                              "height": 1, "image_data_size": 1}
        palette_descriptor = {"format_code": 2, "format": "RGBA16bpp", "width": 2,
                              "height": 1, "image_data_size": 4}
        texture_hash = crc64_resource_path(texture_path)
        display_body = display_list_body([
            f3dex2_wide(0x33, 0, 0, crc64_resource_path(display_path)),
            f3dex2_wide(0x32, (3 << 12) | (3 << 1), 0, crc64_resource_path(array_path)),
            f3dex2_wide(0x20, (2 << 19) | 1, 0, texture_hash),
            f3dex2_command(0xF5, 0, 7 << 24),
            f3dex2_command(0xF3, 0, 7 << 24),
            f3dex2_command(0xF5, 0, 0),
            f3dex2_command(0xD7, 2, (0xFFFF << 16) | 0xFFFF),
            f3dex2_wide(0x20, (2 << 19) | 1, 0, crc64_resource_path(palette_path)),
            f3dex2_command(0xF5, 0, 7 << 24),
            f3dex2_command(0xF0, 0, 7 << 24),
            f3dex2_command(0x05, (0 << 16) | (2 << 8) | 4),
            f3dex2_command(0xDF),
        ])
        mesh = {"type": 2, "polygons": [{"position": [100, 20, -5],
                                           "opaque_display_list": display_path}]}
        result = decode_room_render_data(
            [{"mesh": mesh}], {}, {array_path: array},
            {texture_path: (texture_descriptor, b"\x10", 0, ">"),
             palette_path: (palette_descriptor, b"\x00\x00\x07\xc1", 0, ">")},
            {display_path: {"body": display_body, "version": 0}},
            [display_path, array_path, texture_path, palette_path],
            {display_path: ">"}, 2.0)
        self.assertEqual(result["decoded_triangle_count"], 1)
        triangle = result["triangles"][0]
        self.assertEqual(triangle["material"]["texture_image_path"], texture_path)
        self.assertEqual(triangle["material"]["current_settimg_path"], palette_path)
        self.assertEqual(triangle["material"]["palette_path"], palette_path)
        self.assertEqual(triangle["material"]["texture_image_tmem_words"], 0)
        self.assertTrue(triangle["material"]["texture_enabled"])
        self.assertEqual(triangle["vertices"][0]["position"], [100, 20, -5])
        self.assertEqual(triangle["vertices"][0]["world_position"], [200.0, 40.0, -10.0])
        self.assertEqual(triangle["vertices"][1]["uv_texels"], [10.0, 0.0])
        self.assertEqual(result["texture_rgba8"][texture_path],
                         bytes((0, 255, 0, 255, 0, 0, 0, 0)))

    def test_native_level_export_lowers_to_existing_gfx_and_collision_abi(self):
        display_path = "scenes/fixture/DisplayList"
        array_path = "scenes/fixture/VertexArray"
        texture_path = "scenes/fixture/Texture"
        array = parse_vertex_array(vertex_array_with_three_vertices(), ">", array_path)
        descriptor = {"format_code": 2, "format": "RGBA16bpp", "width": 1,
                      "height": 1, "image_data_size": 2}
        display_body = display_list_body([
            f3dex2_wide(0x33, 0, 0, crc64_resource_path(display_path)),
            f3dex2_wide(0x32, (3 << 12) | (3 << 1), 0, crc64_resource_path(array_path)),
            f3dex2_wide(0x20, (2 << 19) | 1, 0, crc64_resource_path(texture_path)),
            f3dex2_command(0xF5, 0, 7 << 24),
            f3dex2_command(0xF3, 0, 7 << 24),
            f3dex2_command(0xF5, 0, 0),
            f3dex2_command(0xD7, 2, (0xFFFF << 16) | 0xFFFF),
            f3dex2_command(0xDE, 0, 0x09000001),
            f3dex2_command(0x05, (0 << 16) | (2 << 8) | 4),
            f3dex2_command(0xDF),
        ])
        mesh = {"type": 2, "polygons": [{"position": [100, 20, -5],
                                           "opaque_display_list": display_path}]}
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "render" / "hyrule_field_runtime.inc.c"
            result = emit_native_level_include(
                output_path, [{"mesh": mesh}], {array_path: array},
                {texture_path: (descriptor, b"\xf8\x01", 0, ">")},
                {display_path: {"body": display_body, "version": 0}},
                [display_path, array_path, texture_path], {display_path: ">"},
                parse_collision(collision_body(), ">", "fixture/CollisionHeader"),
                2.0, {"position": [1, 2, 3], "yaw": -90})
            generated = output_path.read_text()
            self.assertIn("0x01003006u", generated)  # OTR VTX_HASH lowered to F3DEX2 VTX
            self.assertIn("0xfd100001u", generated)  # OTR SETTIMG_HASH lowered to SETTIMG
            self.assertIn("220", generated)       # polygon translation and world scale are baked
            self.assertIn("const Gfx hyrule_field_opaque_dl[]", generated)
            self.assertIn("COL_TRI_INIT(SURFACE_DEFAULT, 1)", generated)
            self.assertIn("COL_TRI(0, 1, 2)", generated)
            self.assertEqual(result["omitted_segmented_display_list_calls"], 1)
            spawn_header = (output_path.parent / "hyrule_field_spawn.h").read_text()
            self.assertIn("#define HYRULE_FIELD_SPAWN_X 2", spawn_header)
            self.assertIn("#define HYRULE_FIELD_SPAWN_Y 4", spawn_header)
            self.assertIn("#define HYRULE_FIELD_SPAWN_Z 6", spawn_header)
            self.assertIn("#define HYRULE_FIELD_SPAWN_YAW ((s16)-90)", spawn_header)
            compiler = shlex.split(os.environ.get("CC", "cc"))[0]
            if shutil.which(compiler):
                translation_unit = Path(directory) / "generated.c"
                translation_unit.write_text(
                    "#include <ultra64.h>\n"
                    f'#include "{output_path.as_posix()}"\n', encoding="utf-8")
                root = Path(__file__).resolve().parents[1]
                command = shlex.split(os.environ.get("CC", "cc")) + [
                    "-fsyntax-only", "-std=gnu99", f"-I{root / 'include'}", f"-I{root}",
                    "-DVERSION_US", "-DF3DEX_GBI_2", "-DNON_MATCHING=1", "-DAVOID_UB=1",
                    str(translation_unit)]
                compiled = subprocess.run(command, text=True, capture_output=True)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)

    def test_native_level_export_keeps_polygon_transforms_separate_between_rooms(self):
        array_path = "scenes/fixture/shared_vertex_array"
        list_paths = ("scenes/fixture/room0_list", "scenes/fixture/room1_list")
        array = parse_vertex_array(
            struct.pack(">II", 25, 1)
            + struct.pack(">3hH2h4B", 1, 2, 3, 0, 0, 0, 255, 255, 255, 255),
            ">", array_path)
        rooms = []
        display_lists = {}
        endian_by_path = {}
        for mesh_index, (path, position) in enumerate(zip(list_paths, ([100, 0, 0], [-200, 0, 0]))):
            mesh = {"type": 2, "polygons": [{"position": position,
                                               "opaque_display_list": path}]}
            rooms.append({"room": f"room{mesh_index}", "mesh": mesh})
            display_lists[path] = {"body": display_list_body([
                f3dex2_wide(0x32, (1 << 12) | (1 << 1), 0,
                            crc64_resource_path(array_path)),
                f3dex2_command(0xDF),
            ]), "version": 0}
            endian_by_path[path] = ">"

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "kokiri_forest_runtime.inc.c"
            result = emit_native_level_include(
                output_path, rooms, {array_path: array}, {}, display_lists,
                [array_path], endian_by_path, None, 2.0, None, "kokiri_forest")
            generated = output_path.read_text()
            self.assertIn("202", generated)
            self.assertIn("-398", generated)
            self.assertIn("const Gfx kokiri_forest_opaque_dl[]", generated)
            self.assertIn("kokiri_forest_vtx_", generated)
            self.assertNotIn("hyrule_field_vtx_", generated)
            self.assertEqual(result["opaque_calls"], 2)
            self.assertEqual(result["vertex_array_count"], 2)

    def test_world_transform_applies_consistently_to_render_collision_and_spawns(self):
        array_path = "scenes/fixture/vertex_array"
        list_path = "scenes/fixture/room_list"
        array = parse_vertex_array(vertex_array_body(), ">", array_path)
        room = {"room": "room0", "mesh": {"polygons": [{
            "position": [1, 2, 3], "opaque_display_list": list_path}]}}
        display_lists = {list_path: {"body": display_list_body([
            f3dex2_wide(0x32, (1 << 12) | (1 << 1), 0, crc64_resource_path(array_path)),
            f3dex2_command(0xDF),
        ]), "version": 0}}
        collision = {"vertices": [[1, 2, 3], [0, 0, 0], [0, 1, 0]],
                     "polygons": [{"vertex_indices": [0, 1, 2]}]}
        spawn = {"position": [1, 2, 3], "yaw": 90,
                 "kart_position": [0, 0, 1], "return_position": [1, 0, 0]}
        translation = (10.0, 5.0, -3.0)
        transformed_forward = transform_world_point([0, 0, 1], 1, (0, 0, 0), 90)
        self.assertAlmostEqual(transformed_forward[0], 1.0)
        self.assertAlmostEqual(transformed_forward[1], 0.0)
        self.assertAlmostEqual(transformed_forward[2], 0.0)

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "fixture_runtime.inc.c"
            emit_native_level_include(
                output_path, [room], {array_path: array}, {}, display_lists,
                [array_path], {list_path: ">"}, collision, 2.0, spawn,
                "fixture_region", translation, 90.0)
            generated = output_path.read_text(encoding="utf-8")
            self.assertIn("128, -59, -29", generated)
            self.assertIn("COL_VERTEX(16, 9, -5)", generated)
            spawn_header = (output_path.parent / "fixture_region_spawn.h").read_text()
            self.assertIn("#define FIXTURE_REGION_SPAWN_X 16", spawn_header)
            self.assertIn("#define FIXTURE_REGION_SPAWN_Y 9", spawn_header)
            self.assertIn("#define FIXTURE_REGION_SPAWN_Z -5", spawn_header)
            self.assertIn("#define FIXTURE_REGION_SPAWN_YAW ((s16)-180)", spawn_header)
            self.assertIn("#define FIXTURE_REGION_KART_X 12", spawn_header)
            self.assertIn("#define FIXTURE_REGION_KART_Z -3", spawn_header)

    def test_import_output_is_deterministic_and_scene_names_are_region_specific(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive_path = root / "oot.o2r"
            prefix = "scenes/shared/spot04_scene"
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr(prefix + "/spot04_scene",
                                 resource_header(0x4F524F4D) + scene_body(prefix + "/CollisionHeader"))
                archive.writestr(prefix + "/CollisionHeader",
                                 resource_header(0x4F434F4C) + collision_body())

            first = root / "first"
            second = root / "second"
            result = import_archive(archive_path, first, prefix, 1.0)
            import_archive(archive_path, second, prefix, 1.0)
            self.assertEqual(result["region"]["id"], "kokiri_forest")
            self.assertEqual(result["region"]["name"], "Kokiri Forest")
            self.assertTrue((first / "render/kokiri_forest_runtime.inc.c").is_file())
            self.assertIn("const Collision kokiri_forest_collision[]",
                          (first / "render/kokiri_forest_runtime.inc.c").read_text())
            first_files = {path.relative_to(first): path.read_bytes() for path in first.rglob("*")
                           if path.is_file()}
            second_files = {path.relative_to(second): path.read_bytes() for path in second.rglob("*")
                            if path.is_file()}
            self.assertEqual(first_files, second_files)

    def test_archive_world_transform_updates_collision_output_and_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive_path = root / "oot.o2r"
            prefix = "scenes/shared/spot04_scene"
            vertices = [(-10, -1, -10), (10, 0, -10), (10, 20, 10)]
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr(prefix + "/spot04_scene",
                                 resource_header(0x4F524F4D) + scene_body(prefix + "/CollisionHeader"))
                archive.writestr(prefix + "/CollisionHeader",
                                 resource_header(0x4F434F4C) + collision_body(vertices))

            output = root / "transformed"
            result = import_archive(archive_path, output, prefix, 1.0,
                                    world_translation=(100.0, 200.0, -50.0),
                                    world_yaw_degrees=90.0)
            native = (output / result["native_level_include"]).read_text(encoding="utf-8")
            self.assertIn("COL_VERTEX(90, 199, -40)", native)
            self.assertEqual(result["region_bounds"]["min"], [90.0, 199.0, -60.0])
            self.assertEqual(result["region_bounds"]["max"], [110.0, 220.0, -40.0])
            self.assertEqual(result["region"]["world_transform"], {
                "translation": [100.0, 200.0, -50.0],
                "yaw_degrees": 90.0,
                "translation_units": "SM64 world units after scale",
            })
            obj_path = output / result["collision_outputs"][0]["obj"]
            self.assertTrue(obj_path.is_file())
            self.assertIn("v 90.000000 199.000000 -40.000000", obj_path.read_text())

    def test_native_level_export_rejects_unrepresentable_spawn(self):
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "hyrule_field_runtime.inc.c"
            with self.assertRaisesRegex(ImportFailure, "MARIO_POS s16 range"):
                emit_native_level_include(output_path, [], {}, {}, {}, [], {}, None,
                                          1.0, {"position": [32768, 0, 0], "yaw": 0})

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

    def test_vertex_array_decodes_position_fixed_uv_and_color_bytes(self):
        array = parse_vertex_array(vertex_array_body(), ">", "fixture/VertexArray")
        self.assertEqual(array["array_type_name"], "Vertex")
        self.assertEqual(array["count"], 1)
        self.assertEqual(array["vertices"][0]["position"], [12, -34, 56])
        self.assertEqual(array["vertices"][0]["uv_fixed_s10_5"], [320, -640])
        self.assertEqual(array["vertices"][0]["uv_texels"], [10.0, -20.0])
        self.assertEqual(array["vertices"][0]["cn_bytes"], [255, 128, 64, 255])

    def test_texture_header_v0_and_v1_match_declared_payload_lengths(self):
        rgba16 = struct.pack(">IIII", 2, 2, 2, 8) + bytes(8)
        descriptor0 = parse_texture_descriptor(rgba16, ">", 0, "fixture/RGBA16")
        self.assertEqual(descriptor0["format"], "RGBA16bpp")
        self.assertTrue(descriptor0["expected_packed_size_matches"])
        self.assertFalse(descriptor0["payload_decoded"])
        gray4 = struct.pack(">IIIIffI", 5, 1, 1, 1, 2.0, 0.5, 1) + bytes(1)
        descriptor1 = parse_texture_descriptor(gray4, ">", 1, "fixture/Gray4")
        self.assertEqual(descriptor1["flags"], 1)
        self.assertEqual(descriptor1["horizontal_byte_scale"], 2.0)
        self.assertEqual(descriptor1["vertical_pixel_scale"], 0.5)
        self.assertTrue(descriptor1["expected_packed_size_matches"])

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
                archive.writestr(prefix + "/VertexArray", resource_header(0x4F415252) + vertex_array_body())
                texture_body = struct.pack(">IIII", 2, 2, 2, 8) + bytes(8)
                archive.writestr(prefix + "/Texture", resource_header(0x4F544558) + texture_body)
                archive.writestr("objects/object_spot00_objects/Texture", b"fixture only")
                archive.writestr(prefix + "/source-info.bin", b"headerless fixture")
            output = root / "converted"
            result = import_archive(archive_path, output, prefix, 1.5)
            manifest = json.loads((output / "hyrule-world.json").read_text())
            self.assertEqual(manifest["format_version"], 1)
            self.assertEqual(manifest["archive"]["entry_count"], 7)
            self.assertEqual(result["region"]["scene_resource"], prefix + "/spot00_scene")
            self.assertEqual(result["collision_outputs"][0]["triangles"], 1)
            self.assertEqual(result["decoded_resource_count"], 4)
            self.assertEqual(result["parsed_resource_count"], 5)
            self.assertEqual(result["vertex_arrays"][0]["vertices"][0]["position"], [12, -34, 56])
            self.assertEqual(result["textures"][0]["format"], "RGBA16bpp")
            self.assertEqual(result["resource_type_counts"]["headerless-or-short"], 1)
            self.assertEqual(result["dependencies"][0]["status"], "available")
            self.assertEqual(result["region_bounds"], {"min": [-15.0, -1.5, -15.0],
                                                         "max": [15.0, 30.0, 15.0],
                                                         "source": "collision headers"})
            self.assertEqual([entry["path"] for entry in result["resource_index"]],
                             sorted(entry["path"] for entry in result["resource_index"]))
            self.assertIn("v 150.000000 0.000000 0.000000", (output / "spot00.collision.obj").read_text())
            self.assertTrue((output / "render/hyrule_field_runtime.inc.c").is_file())
            self.assertEqual(manifest["native_level_include_summary"]["collision_triangles"], 1)

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
