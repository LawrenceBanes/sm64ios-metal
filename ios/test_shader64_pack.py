#!/usr/bin/env python3
"""Bounded ZIP and source-contract tests for the SHADER64 installer."""
import ctypes
import json
import os
import pathlib
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/pc/gfx/gfx_shader64_pack.c"
VALID = b'''#include <metal_stdlib>\nusing namespace metal;\nfloat4 shader64_post(float4 color, float2 uv, texture2d<float> image, sampler imageSampler, constant float4 &params) { return floor(color * params.x * 8.0) / 8.0; }\n'''


class ShaderPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="shader64-tests-")
        cls.lib = pathlib.Path(cls.temp.name) / "libshader64.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-I" + str(ROOT / "include"), str(SOURCE), "-o", str(cls.lib)], check=True)
        cls.api = ctypes.CDLL(str(cls.lib))
        cls.api.gfx_shader64_install_zip.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_install_zip.restype = ctypes.c_int
        cls.api.gfx_shader64_validate_post_source.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_validate_post_source.restype = ctypes.c_int
        cls.api.gfx_shader64_build_wrapper.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_build_wrapper.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def archive(self, entries):
        out = pathlib.Path(self.temp.name) / "fixture.zip"
        with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for name, data in entries:
                zf.writestr(name, data)
        return out.read_bytes()

    def install(self, archive):
        destination = pathlib.Path(tempfile.mkdtemp(prefix="shader64-pack-"))
        raw = ctypes.create_string_buffer(archive)
        error = ctypes.create_string_buffer(256)
        ok = self.api.gfx_shader64_install_zip(raw, len(archive), os.fsencode(destination), error, len(error))
        return bool(ok), error.value.decode(), destination

    def test_imports_only_recognized_deflated_pack_files(self):
        data = self.archive([("manifest.json", b'{"shaderApiVersion":1}'),
                             ("shaders/postprocess.metal", VALID),
                             ("shaders/", b"")])
        ok, error, path = self.install(data)
        self.assertTrue(ok, error)
        self.assertEqual((path / "shaders/postprocess.metal").read_bytes(), VALID)
        self.assertEqual((path / "manifest.json").read_bytes(), b'{"shaderApiVersion":1}')

    def test_imports_bounded_precompiled_metallib(self):
        binary = b"MTLB" + bytes(range(64))
        manifest = json.dumps({"formatVersion": 1, "shaderApiVersion": 1,
            "stage": "postprocess", "source": "shaders/postprocess.metallib"}).encode()
        ok, error, path = self.install(self.archive([
            ("manifest.json", manifest), ("shaders/postprocess.metallib", binary)]))
        self.assertTrue(ok, error)
        self.assertEqual((path / "shaders/postprocess.metallib").read_bytes(), binary)

    def test_rejects_oversized_precompiled_metallib(self):
        manifest = b'{"formatVersion":1}'
        binary = b"M" * (4 * 1024 * 1024 + 1)
        ok, error, _ = self.install(self.archive([
            ("manifest.json", manifest), ("shaders/postprocess.metallib", binary)]))
        self.assertFalse(ok)
        self.assertIn("size limit", error)

    def test_rejects_unsafe_and_unrecognized_entries(self):
        for entries in (
            [("manifest.json", b"{}"), ("shaders/a.metal", VALID), ("../escape", b"x")],
            [("manifest.json", b"{}"), ("shaders/a.metal", VALID), ("payload.bin", b"x")],
        ):
            data = self.archive(entries)
            ok, _, _ = self.install(data)
            self.assertFalse(ok)

    def test_rejects_archive_without_manifest_or_shader(self):
        data = self.archive([("shaders/a.metal", VALID)])
        ok, error, _ = self.install(data)
        self.assertFalse(ok)
        self.assertIn("manifest", error)

    def test_corrupt_payload_cleans_partial_install(self):
        data = bytearray(self.archive([("manifest.json", b'{"formatVersion":1}'),
                                       ("shaders/postprocess.metal", VALID)]))
        name = b"shaders/postprocess.metal"
        offset = data.find(b"PK\x03\x04", data.find(name) - 30)
        self.assertGreaterEqual(offset, 0)
        name_len, extra_len = struct.unpack_from("<HH", data, offset + 26)
        data[offset + 30 + name_len + extra_len + 2] ^= 0x20
        ok, error, path = self.install(bytes(data))
        self.assertFalse(ok)
        self.assertTrue(error)
        self.assertFalse((path / "manifest.json").exists())
        self.assertFalse((path / "shaders").exists())

    def test_rejects_truncated_zip(self):
        ok, _, _ = self.install(self.archive([("manifest.json", b"{}"),
                                              ("shaders/postprocess.metal", VALID)])[:-9])
        self.assertFalse(ok)

    def test_post_shader_contract(self):
        error = ctypes.create_string_buffer(256)
        self.assertTrue(self.api.gfx_shader64_validate_post_source(VALID, len(VALID), error, len(error)))
        wrapper = ctypes.create_string_buffer(len(VALID) + 1024)
        self.assertTrue(self.api.gfx_shader64_build_wrapper(VALID, len(VALID), wrapper, len(wrapper)))
        self.assertIn(b"sm64_shader64_fragment", wrapper.value)
        self.assertIn(b"return shader64_post(color,in.uv,image,imageSampler,parameters)", wrapper.value)
        bad = VALID.replace(b"shader64_post", b"wrong_name")
        self.assertFalse(self.api.gfx_shader64_validate_post_source(bad, len(bad), error, len(error)))

    def test_ios_runtime_vertical_slice_wiring(self):
        metal = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
        ui = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
        for token in ("newLibraryWithSource:wrapped", "newLibraryWithData:bytes", "gfx_shader64_build_wrapper",
                      "gfx_metal_load_shader64_library", "postprocess.metallib",
                      "metal_shader64_pipeline", "shader64_parameters", "gfx_metal_use_classic_shader",
                      "gfx_metal_restore_shader64_pack", "SM64Shader64ActivePack"):
            self.assertIn(token, metal)
        for token in ("UIDocumentPickerViewController", "shaderPackRoot", "shaderpacks",
                      "gfx_shader64_install_zip", "shader64_post", "Delete an installed pack",
                      "Configure %@", "gfx_metal_use_classic_shader", "UISlider", "UISwitch",
                      "UISegmentedControl", "type must be float, boolean, or select",
                      "Reset to Defaults", "About %@", "Shader API %@"):
            self.assertIn(token, ui)
        importer_start = ui.index("- (void)installShaderArchiveAtURL:(NSURL *)url {")
        importer = ui[importer_start:ui.index("\n}", importer_start) + 2]
        self.assertLess(importer.index("getResourceValue:&fileSize"), importer.index("dataWithContentsOfURL:url"))
        for pack in ("Classic64", "Paper64", "Lumen64"):
            directory = ROOT / "ios/shaderpacks" / pack
            manifest = json.loads((directory / "manifest.json").read_text())
            self.assertEqual(manifest["shaderApiVersion"], 1)
            self.assertTrue(self.api.gfx_shader64_validate_post_source(
                (directory / "shaders/postprocess.metal").read_bytes(),
                len((directory / "shaders/postprocess.metal").read_bytes()),
                ctypes.create_string_buffer(256), 256))

    def test_asan_ubsan_zip_paths(self):
        binary = pathlib.Path(self.temp.name) / "shader64-pack-sanitized"
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src/pc/gfx"),
                        str(ROOT / "tests/shader64_pack_smoke.c"), str(SOURCE),
                        "-o", str(binary)], check=True)
        valid = self.archive([("manifest.json", b'{"formatVersion":1}'),
                              ("shaders/postprocess.metal", VALID)])
        invalid = valid[:-11]
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        for index, (payload, expected) in enumerate(((valid, "1"), (invalid, "0"))):
            archive_path = pathlib.Path(self.temp.name) / f"sanitized-{index}.zip"
            destination = pathlib.Path(self.temp.name) / f"sanitized-pack-{index}"
            archive_path.write_bytes(payload)
            subprocess.run([str(binary), str(archive_path), str(destination), expected],
                           check=True, env=env, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
