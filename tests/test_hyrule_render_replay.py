"""Exercise O2R -> native DL -> external base pack -> renderer upload on Linux."""
import json
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest
import zipfile

from test_hyrule_importer import (
    crc64_resource_path, display_list_body, f3dex2_command, f3dex2_wide, import_archive,
    resource_header, vertex_array_with_three_vertices,
)

ROOT = Path(__file__).resolve().parents[1]


def make_scene(archive):
    prefix = "scenes/fixture"
    room, dl, vtx, tex = [prefix + "/" + leaf for leaf in ("room", "dl", "vertices", "image")]

    def string(path):
        return struct.pack(">I", len(path)) + path.encode()

    def put(path, kind, payload):
        archive.writestr(path, resource_header(kind) + payload)

    end = struct.pack(">i", 20)
    put(prefix + "/Scene", 0x4F524F4D, struct.pack(">Iii", 2, 4, 1) + string(room) + bytes(8) + end)
    # A type-2 culling sphere deliberately far from the local triangle.
    put(room, 0x4F524F4D, struct.pack(">Ii4b4h", 2, 10, 0, 2, 1, 2, 32000, 100, 800, 32000)
        + string(dl) + string(dl) + end)
    put(vtx, 0x4F415252, vertex_array_with_three_vertices())
    put(tex, 0x4F544558, struct.pack(">IIII", 2, 1, 2, 4) + bytes.fromhex("f80107c1"))
    put(dl, 0x4F444C54, display_list_body([
        f3dex2_wide(0x20, 2 << 19, 0, crc64_resource_path(tex)),
        f3dex2_command(0xF5, 2 << 19, 7 << 24),
        f3dex2_command(0xF3, 0, (7 << 24) | (1 << 12)),
        f3dex2_command(0xF5, (2 << 19) | (1 << 9), 0),
        f3dex2_command(0xF2, 0, 4),
        f3dex2_command(0xD7, 2, 0xffffffff),
        f3dex2_command(0xFC, 0x267e04, 0x1f0cfdff),  # source two-cycle water combiner
        f3dex2_command(0xFA, 0, 0xffffff66),
        f3dex2_command(0xE2, 0x1c, 0xc81049d8),
        f3dex2_command(0xD9, 0xfffbff, 0x10000),  # fog on, cull off
        f3dex2_wide(0x32, (3 << 12) | 6, 0, crc64_resource_path(vtx)),
        f3dex2_command(0x05, 0x204),
        f3dex2_command(0xDF),
    ]))


class HyruleRenderTest(unittest.TestCase):
    def test_imported_field_at_backend_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            archive = work / "fixture.o2r"
            with zipfile.ZipFile(archive, "w") as bundle:
                make_scene(bundle)
            output = work / "build/hyrule_field_import"
            private_render = os.environ.get("HYRULE_GENERATED_RENDER_DIR")
            if private_render:
                output.parent.mkdir()
                output.symlink_to(Path(private_render).resolve().parent)
            else:
                import_archive(archive, output, "scenes/fixture", 1.0)
            render = output / "render"
            if not private_render:
                triangles = json.loads((render / "hyrule-render.json").read_text())["triangles"]
                self.assertTrue(triangles)
                self.assertEqual(triangles[0]["material"]["other_mode_l"], 0xc81049d8)

            # Use the shipping Make packaging recipe with an existing executable
            # and four placeholder audio files. Skip the unrelated ROM/tool build.
            (work / "Makefile.split").symlink_to(ROOT / "Makefile.split")
            for name in ("actors", "levels", "textures", "build/us_pc/sound"):
                (work / name).mkdir(parents=True)
            for name in ("bank_sets", "sequences.bin", "sound_data.ctl", "sound_data.tbl"):
                (work / "build/us_pc/sound" / name).touch()
            exe = work / "test-game"
            exe.touch()
            make_env = {**os.environ, "GIT_DIR": str(ROOT / ".git")}
            make = ["make", "-s", "-f", str(ROOT / "Makefile"), "res", "MAKECMDGOALS=clean",
                    "NOEXTRACT=1", "EXTERNAL_DATA=1", "EXE=" + str(exe), "-o", str(exe),
                    "TOOLS_DIR=" + str(ROOT / "tools")]
            subprocess.run(make + ["IOS_HYRULE_WORLD=1"], cwd=work, env=make_env, check=True)
            pack = work / "build/us_pc/res/base.zip"
            required = (render / "hyrule-basepack.lst").read_text().splitlines()
            with zipfile.ZipFile(pack) as z:
                self.assertCountEqual(required, [n for n in z.namelist() if n.startswith("gfx/")])
                for entry in required:
                    self.assertEqual(z.read(entry), (render / entry).read_bytes())

            if private_render:
                manifest = json.loads((render / "hyrule-render.json").read_text())
                floor_textures = {}
                for triangle in manifest["triangles"]:
                    points = [vertex["world_position"] for vertex in triangle["vertices"]]
                    a = [points[1][i] - points[0][i] for i in range(3)]
                    b = [points[2][i] - points[0][i] for i in range(3)]
                    normal_y = a[2] * b[0] - a[0] * b[2]
                    normal_length = math.sqrt(sum(value * value for value in (
                        a[1] * b[2] - a[2] * b[1], normal_y,
                        a[0] * b[1] - a[1] * b[0])))
                    material = triangle["material"]
                    if normal_length and normal_y / normal_length > 0.6 and material["texture_enabled"]:
                        path = material["texture_image_path"]
                        floor_textures[path] = floor_textures.get(path, 0) + 1
                self.assertTrue(floor_textures, "imported field has no upward textured triangles")
                principal_floor_texture = max(floor_textures, key=floor_textures.get)
                floor_entry = next(entry for entry in required if
                                   Path(entry).name.startswith(f"{crc64_resource_path(principal_floor_texture):016x}."))
                record = next(item for item in manifest["texture_records"]
                              if item["path"] == principal_floor_texture)
                rgba = (render / record["rgba8_file"]).read_bytes()
                self.assertTrue(all(alpha == 255 for alpha in rgba[3::4]),
                                "principal floor texture unexpectedly has transparent texels")
                self.assertIn(floor_entry, required)

            executable = work / "replay"
            sources = ["tests/hyrule_render_replay.c", "src/pc/gfx/gfx_cc.c",
                       "src/pc/gfx/gfx_scene_capture.c", "src/pc/platform.c",
                       "src/engine/graph_node_manager.c", "src/engine/math_util.c",
                       "src/pc/mods/dynos_actor.c", "src/pc/mods/dynos_actor_native.c"]
            sources.extend(sorted((ROOT / "src/pc/fs").glob("*.c")))
            subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
                "-std=gnu99", "-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-DF3DEX_GBI_2E", "-DNON_MATCHING", "-DAVOID_UB", "-DVERSION_US", "-DEXTERNAL_DATA",
                "-Iinclude", "-Isrc", "-I.", "-I" + str(render),
                *([] if private_render else ["-DREPLAY_FIXTURE"]), *sources, "-lm", "-o", executable
            ], cwd=ROOT, check=True)
            replay_env = os.environ.copy()
            private_mod = os.environ.get("MOD_EXTERNAL_ZIP")
            if private_mod:
                with zipfile.ZipFile(private_mod) as source:
                    actors = sorted(name for name in source.namelist()
                                    if name.endswith(".bin") and "/actors/" in name)
                    if actors:
                        actor_path = work / "dynos-replay.bin"
                        actor_path.write_bytes(source.read(actors[0]))
                        replay_env["DYNOS_REPLAY_BUNDLE"] = str(actor_path)
            subprocess.run([executable, pack], check=True, env=replay_env)

            # Simulate an incomplete private pack: the renderer must upload
            # a visible, opaque Hyrule diagnostic material for a sampled draw.
            manifest = json.loads((render / "hyrule-render.json").read_text())
            sampled = next(t["material"]["texture_image_path"] for t in manifest["triangles"]
                           if t["material"]["texture_enabled"] and t["material"]["texture_image_path"])
            missing_entry = next(entry for entry in required if
                                 Path(entry).name.startswith(f"{crc64_resource_path(sampled):016x}."))
            incomplete_pack = work / "incomplete-base.zip"
            with zipfile.ZipFile(pack) as source, zipfile.ZipFile(incomplete_pack, "w") as destination:
                for entry in source.infolist():
                    if entry.filename != missing_entry:
                        destination.writestr(entry, source.read(entry))
            subprocess.run([executable, incomplete_pack], check=True,
                           env={**os.environ, "REPLAY_EXPECT_MISSING_HYRULE_TEXTURE": "1"})

            # The Make mode stamp must invalidate the pack in a reused build dir.
            subprocess.run(make + ["IOS_HYRULE_WORLD=0"], cwd=work, env=make_env, check=True)
            with zipfile.ZipFile(pack) as z:
                self.assertFalse(any(n.startswith("gfx/levels/hyrule_field/") for n in z.namelist()))


if __name__ == "__main__":
    unittest.main()
