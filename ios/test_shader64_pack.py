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
import math

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
        class GraphPass(ctypes.Structure):
            _fields_ = [("id", ctypes.c_char_p), ("input", ctypes.c_char_p),
                        ("output", ctypes.c_char_p), ("resolution_scale", ctypes.c_float),
                        ("node_type", ctypes.c_uint), ("resource_flags", ctypes.c_uint)]
        cls.GraphPass = GraphPass
        cls.api.gfx_shader64_validate_graph.argtypes = [ctypes.POINTER(GraphPass), ctypes.c_size_t,
            ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_validate_graph.restype = ctypes.c_int
        cls.api.gfx_shader64_graph_target_size.argtypes = [ctypes.c_uint, ctypes.c_uint,
            ctypes.c_float, ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
        cls.api.gfx_shader64_graph_target_size.restype = ctypes.c_int
        cls.api.gfx_shader64_validate_pass_source.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_validate_pass_source.restype = ctypes.c_int
        cls.api.gfx_shader64_validate_material_source.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_validate_material_source.restype = ctypes.c_int
        cls.api.gfx_shader64_build_pass_wrapper.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_build_pass_wrapper.restype = ctypes.c_int
        cls.api.gfx_shader64_validate_depth_pass_source.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_validate_depth_pass_source.restype = ctypes.c_int
        cls.api.gfx_shader64_build_depth_pass_wrapper.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t]
        cls.api.gfx_shader64_build_depth_pass_wrapper.restype = ctypes.c_int

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

    def test_material_shader_contract(self):
        source = (b'#include <metal_stdlib>\nusing namespace metal;\n'
                  b'float4 shader64_material(float4 baseColor, float2 uv, float4 screenPosition, '
                  b'uint materialClass, constant float4 &params) { '
                  b'if (materialClass == 3u) baseColor.rgb *= params.x; return baseColor; }')
        error = ctypes.create_string_buffer(256)
        self.assertTrue(self.api.gfx_shader64_validate_material_source(source, len(source), error, len(error)), error.value)
        self.assertFalse(self.api.gfx_shader64_validate_material_source(
            source.replace(b"shader64_material", b"wrong_material"), len(source), error, len(error)))

    def test_scene_depth_pass_contract_and_wrapper(self):
        source = (b'#include <metal_stdlib>\nusing namespace metal;\n'
                  b'float4 shader64_pass_depth(float4 color, float2 uv, texture2d<float> image, '
                  b'sampler imageSampler, depth2d<float> sceneDepth, sampler depthSampler, '
                  b'constant float4x4 &inverseProjection, constant float4 &params) { '
                  b'float d=sceneDepth.sample(depthSampler,uv); return float4(d,d,d,1.0); }')
        error = ctypes.create_string_buffer(256)
        self.assertTrue(self.api.gfx_shader64_validate_depth_pass_source(
            source, len(source), error, len(error)), error.value)
        wrapper = ctypes.create_string_buffer(len(source) + 2048)
        self.assertTrue(self.api.gfx_shader64_build_depth_pass_wrapper(
            source, len(source), wrapper, len(wrapper)))
        self.assertIn(b"depth2d<float> sceneDepth [[texture(1)]]", wrapper.value)
        self.assertIn(b"constant float4x4 &inverseProjection [[buffer(1)]]", wrapper.value)
        invalid = source.replace(b"depth2d<float>", b"texture2d<float>")
        self.assertFalse(self.api.gfx_shader64_validate_depth_pass_source(
            invalid, len(invalid), error, len(error)))

    def test_api2_graph_topology_and_bounds(self):
        passes = (self.GraphPass * 2)(
            self.GraphPass(b"grade", b"sceneColor", b"graded", 0.5, 1),
            self.GraphPass(b"presenter", b"graded", b"present", 1.0, 1))
        order = (ctypes.c_ubyte * 4)()
        error = ctypes.create_string_buffer(256)
        self.assertTrue(self.api.gfx_shader64_validate_graph(passes, 2, 1920, 1080, order, error, len(error)), error.value)
        self.assertEqual(list(order)[:2], [0, 1])
        wrapper = ctypes.create_string_buffer(len(VALID) + 1024)
        source = VALID.replace(b"shader64_post", b"shader64_pass")
        self.assertTrue(self.api.gfx_shader64_validate_pass_source(source, len(source), error, len(error)))
        self.assertTrue(self.api.gfx_shader64_build_pass_wrapper(source, len(source), wrapper, len(wrapper)))
        self.assertIn(b"return shader64_pass", wrapper.value)

    def test_api2_graph_rejects_cycles_feedback_and_unbounded_targets(self):
        error = ctypes.create_string_buffer(256)
        order = (ctypes.c_ubyte * 4)()
        cases = [
            (self.GraphPass * 2)(self.GraphPass(b"a", b"bOut", b"aOut", 0.5, 1),
                                 self.GraphPass(b"b", b"aOut", b"present", 1, 1)),
            (self.GraphPass * 2)(self.GraphPass(b"a", b"present", b"aOut", 0.5, 1),
                                 self.GraphPass(b"b", b"aOut", b"present", 1, 1)),
        ]
        for passes in cases:
            self.assertFalse(self.api.gfx_shader64_validate_graph(passes, 2, 1920, 1080, order, error, len(error)))
        invalid_resource = (self.GraphPass * 1)(
            self.GraphPass(b"final", b"sceneColor", b"present", 1.0, 1, 2))
        self.assertFalse(self.api.gfx_shader64_validate_graph(
            invalid_resource, 1, 1920, 1080, order, error, len(error)))
        oversized = (self.GraphPass * 3)(self.GraphPass(b"a", b"sceneColor", b"aOut", 1, 1),
            self.GraphPass(b"b", b"aOut", b"bOut", 1, 1),
            self.GraphPass(b"c", b"bOut", b"present", 1, 1))
        self.assertFalse(self.api.gfx_shader64_validate_graph(oversized, 3, 2048, 1080, order, error, len(error)))

    def test_graph_float_scale_bounds_and_pixel_rounding(self):
        def float_from_bits(bits):
            return struct.unpack("<f", struct.pack("<I", bits))[0]

        target_width, target_height = ctypes.c_uint(), ctypes.c_uint()
        valid_scales = (0.25, float_from_bits(0x3e800001), 0.5,
                        float_from_bits(0x3f7fffff), 1.0)
        for scale in valid_scales:
            self.assertTrue(self.api.gfx_shader64_graph_target_size(
                8192, 4321, scale, ctypes.byref(target_width), ctypes.byref(target_height)))
            self.assertEqual(target_width.value, math.floor(8192 * float(ctypes.c_float(scale).value)))
            self.assertEqual(target_height.value, math.floor(4321 * float(ctypes.c_float(scale).value)))
        # The previous renderer multiplied NSUInteger by float, promoting the
        # integer dimension to float first. This product rounds up to 69 in
        # float, while the exact double product of the encoded float is below
        # 69; target allocation and the 32 MiB budget must agree on 68 pixels.
        rounding_scale = float_from_bits(0x3ef3a431)
        self.assertEqual(float(ctypes.c_float(rounding_scale).value), rounding_scale)
        self.assertTrue(self.api.gfx_shader64_graph_target_size(
            145, 145, rounding_scale, ctypes.byref(target_width), ctypes.byref(target_height)))
        self.assertEqual((target_width.value, target_height.value), (68, 68))
        self.assertFalse(self.api.gfx_shader64_graph_target_size(
            8192, 4321, float_from_bits(0x3e7fffff), ctypes.byref(target_width), ctypes.byref(target_height)))
        self.assertFalse(self.api.gfx_shader64_graph_target_size(
            8192, 4321, float_from_bits(0x3f800001), ctypes.byref(target_width), ctypes.byref(target_height)))
        self.assertFalse(self.api.gfx_shader64_graph_target_size(
            0, 4321, 0.5, ctypes.byref(target_width), ctypes.byref(target_height)))

        # At the maximum legal viewport a quarter-scale RGBA16Float target is
        # exactly 32 MiB. The budget must account for the allocated pixel
        # dimensions, not a floating estimate that truncates or rounds oddly.
        passes = (self.GraphPass * 2)(
            self.GraphPass(b"quarter", b"sceneColor", b"quarterOut", 0.25, 1),
            self.GraphPass(b"presenter", b"quarterOut", b"present", 1.0, 1))
        order = (ctypes.c_ubyte * 4)()
        error = ctypes.create_string_buffer(256)
        self.assertTrue(self.api.gfx_shader64_validate_graph(
            passes, 2, 8192, 8192, order, error, len(error)), error.value)

    def test_production_importer_accepts_every_reference_pack(self):
        reference_ids = ("Classic64", "Paper64", "Lumen64", "GraphDemo", "BlissGridlines", "DepthDebug64")
        for pack_id in reference_ids:
            directory = ROOT / "ios/shaderpacks" / pack_id
            manifest = json.loads((directory / "manifest.json").read_text())
            error = ctypes.create_string_buffer(256)
            entries = [("manifest.json", (directory / "manifest.json").read_bytes())]
            if manifest["shaderApiVersion"] == 1:
                definitions = [{"source": manifest["source"]}]
            else:
                definitions = manifest["passes"]
            for definition in definitions:
                source = (directory / definition["source"]).read_bytes()
                if manifest["shaderApiVersion"] == 1:
                    self.assertTrue(self.api.gfx_shader64_validate_post_source(
                        source, len(source), error, len(error)), f"{pack_id}: {error.value!r}")
                else:
                    depth_pass = bool(definition.get("sceneDepth"))
                    validator = self.api.gfx_shader64_validate_depth_pass_source if depth_pass else self.api.gfx_shader64_validate_pass_source
                    self.assertTrue(validator(source, len(source), error, len(error)),
                                    f"{pack_id}: {error.value!r}")
                    wrapper = ctypes.create_string_buffer(len(source) + 1024)
                    builder = self.api.gfx_shader64_build_depth_pass_wrapper if depth_pass else self.api.gfx_shader64_build_pass_wrapper
                    self.assertTrue(builder(source, len(source), wrapper, len(wrapper)))
                    scale = ctypes.c_float(definition["scale"]).value
                    self.assertTrue(math.isfinite(scale) and 0.25 <= scale <= 1.0,
                                    f"{pack_id}: scale {scale} outside production bounds")
                entries.append((definition["source"], source))
            if manifest["shaderApiVersion"] == 2:
                graph_passes = (self.GraphPass * len(definitions))(*[
                    self.GraphPass(p["id"].encode(), p["input"].encode(), p["output"].encode(),
                                   ctypes.c_float(p["scale"]).value, 1,
                                   1 if p.get("sceneDepth") else 0) for p in definitions])
                order = (ctypes.c_ubyte * 4)()
                self.assertTrue(self.api.gfx_shader64_validate_graph(
                    graph_passes, len(definitions), 1920, 1080, order, error, len(error)),
                    f"{pack_id}: {error.value!r}")
                material_definition = manifest.get("materialShader")
                if material_definition is not None:
                    self.assertEqual(pack_id, "GraphDemo", "unexpected material hook in reference pack")
                    material_path = material_definition["source"]
                    self.assertTrue(material_path.startswith("shaders/") and ".." not in material_path)
                    material_source = (directory / material_path).read_bytes()
                    self.assertTrue(self.api.gfx_shader64_validate_material_source(
                        material_source, len(material_source), error, len(error)),
                        f"{pack_id} material hook: {error.value!r}")
                    entries.append((material_path, material_source))
            for parameter in manifest.get("parameters", []):
                if parameter.get("type") == "float":
                    minimum, maximum, default = (float(parameter[k]) for k in ("min", "max", "default"))
                    self.assertTrue(all(math.isfinite(value) for value in (minimum, maximum, default)))
                    self.assertLessEqual(minimum, default)
                    self.assertLessEqual(default, maximum)
                    self.assertGreaterEqual(minimum, -16)
                    self.assertLessEqual(maximum, 16)
                    if pack_id == "GraphDemo" and parameter["name"] == "levels":
                        self.assertEqual((minimum, default, maximum), (2.0, 6.0, 16.0))
            ok, message, installed = self.install(self.archive(entries))
            self.assertTrue(ok, f"{pack_id}: {message}")
            for name, data in entries:
                self.assertEqual((installed / name).read_bytes(), data, pack_id)
        self.assertTrue((ROOT / "ios/shaderpacks/BlissGridlines/LICENSE.md").exists())

    def test_ios_runtime_vertical_slice_wiring(self):
        metal = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
        ui = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
        for token in ("newLibraryWithSource:wrapped", "newLibraryWithData:bytes", "gfx_shader64_build_wrapper",
                      "gfx_metal_load_shader64_library", "postprocess.metallib",
                      "metal_shader64_pipeline", "shader64_parameters", "gfx_metal_use_classic_shader",
                      "gfx_metal_restore_shader64_pack", "SM64Shader64ActivePack",
                      "gfx_metal_load_shader64_graph_with_material", "gfx_metal_build_shader_with_material",
                      "gfx_metal_shader64_needs_scene_depth"):
            self.assertIn(token, metal)
        pack_header = (ROOT / "src/pc/gfx/gfx_shader64_pack.h").read_text()
        self.assertIn("GFX_SHADER64_RESOURCE_SCENE_DEPTH", pack_header)
        self.assertIn("depth2d<float>", (ROOT / "src/pc/gfx/gfx_shader64_pack.c").read_text())
        self.assertIn("shader64_material(color", (ROOT / "src/pc/gfx/gfx_metal_shader.c").read_text())
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
        for token in ("gfx_metal_load_shader64_graph", "gfx_shader64_validate_graph",
                      "GFX_SHADER64_MAX_GRAPH_PASSES", "GFX_SHADER64_PARAMETER_ABS_MAX", "render_graph"):
            self.assertIn(token, (ROOT / "src/pc/gfx/gfx_metal.m").read_text() + ui)
        apple_script = (ROOT / "ios/validate-metal-apple.sh").read_text()
        self.assertIn("shader64_wrap_v2.c", apple_script)
        self.assertIn("shader64-api2-", apple_script)

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
