"""Original fixtures for reconstructed pass state, generated calls and WSC2 lowering."""

from tempfile import TemporaryDirectory
from pathlib import Path
import json
import unittest
from zipfile import ZipFile

from test_o2r_draw_trace import (
    OPA, ROOM, ROOT, VTX, XLU, display_list, fixture, packet, ref, scene, text, trace,
)
from tools import fast3d_material as material, fast3d_pass_state as setup, wsc_material_lowering as lowering


class PassTests(unittest.TestCase):
    def trace_lists(self, opaque, translucent, context=None, room_type=0, **entries):
        archive_entries = {**fixture(), OPA: display_list(*opaque), XLU: display_list(*translucent), **entries}
        archive_entries[ROOM] = scene((8, bytes((room_type,)) + bytes(4)),
                                     (10, bytes((0, 0, 1, 0)) + text(OPA) + text(XLU)))
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "fixture.zip"
            with ZipFile(path, "w") as archive:
                for name, value in archive_entries.items():
                    archive.writestr(name, value)
            with ZipFile(path) as archive:
                return trace.DrawTracer(trace.ArchiveResources(archive), ROOT, profile=context).run()

    def test_shared_setup_and_pass_overrides(self):
        # Decode original triangles whose combiner needs no initial texture.
        commands = (packet(0xFC, 0x00FFFFFF, 0xFFFE791C),
                    ref(0x32, VTX, 0x3006), packet(0x05, 0x000204))
        result = self.trace_lists(commands, (packet(0xE2, 0x001A00), *commands),
                                  setup.KokiriInputs(0, 0, False, 0), room_type=3)
        opa, xlu = result["draws"]
        self.assertFalse(result["issues"])
        self.assertEqual([d["pass"] for d in (opa, xlu)], ["opaque", "translucent"])
        self.assertEqual(opa["geometry"]["mode"], 0x230405)
        self.assertEqual(opa["render_state"]["other_mode_high"], 0x182CD0)
        self.assertEqual(xlu["render_state"]["other_mode_high"], 0x182C10)
        self.assertEqual(opa["render_state"]["other_mode_low"], 0xC8112078)
        self.assertTrue(opa["render_state"]["depth_write"])
        self.assertFalse(xlu["render_state"]["depth_write"])
        self.assertEqual(opa["combiner"]["colors"]["environment"]["rgba"], [128] * 4)

    def test_dynamic_segments_are_local_to_pass_and_keep_provenance(self):
        base = (packet(0xFC, 0x00FFFFFF, 0xFFFE791C), ref(0x32, VTX, 0x3006), packet(0x05, 0x000204))
        opaque = (packet(0xDE, 0, 0x0A000001), *base)
        translucent = (packet(0xDE, 0, 0x08000001), packet(0xDE, 0, 0x0B000001), *base,
                       packet(0xDE, 0, 0x0A000001), *base)
        context = setup.KokiriInputs(37, 4, True, 20)
        result = self.trace_lists(opaque, translucent, context)
        a, b, c = result["draws"]
        self.assertEqual(a["combiner"]["colors"]["environment"]["rgba"], [128, 128, 128, 235])
        self.assertEqual(b["combiner"]["colors"]["environment"]["rgba"], [128, 128, 128, 50])
        self.assertEqual(a["combiner"]["colors"]["environment"]["source"], setup.SCENE_CONFIG)
        self.assertEqual(a["combiner"]["colors"]["environment"]["origin"], "reconstructed")
        self.assertEqual(len(result["generated_calls"]), 3)
        self.assertIn("unresolved:DL", c["missing"])
        self.assertEqual(result["coverage"]["issues"], {"unresolved": 1})
        repeated = self.trace_lists(opaque, translucent, context)
        self.assertEqual(json.dumps(result, sort_keys=True), json.dumps(repeated, sort_keys=True))

    def test_scroll_and_layer_arithmetic(self):
        context = setup.KokiriInputs(37, 6, True, 150)
        opaque = context.prelude("opaque", 0)
        translucent = context.prelude("translucent", 0)
        self.assertEqual(opaque.bindings[0x0B].packets[1][1], 0x80808041)
        self.assertEqual(trace.rect(*opaque.bindings[0x0C].packets[1]), [0, 2045, 124, 2105])
        self.assertEqual(trace.rect(*translucent.bindings[8].packets[1]), [90, 114, 214, 238])
        self.assertEqual(trace.rect(*translucent.bindings[8].packets[3]), [37, 114, 161, 238])
        self.assertEqual(trace.rect(*translucent.bindings[9].packets[1]), [90, 37, 214, 161])
        self.assertEqual(setup.KokiriInputs(0, 0, True, 0).prelude("opaque", 0)
                         .bindings[11].packets[1][1], 0x808080D7)
        self.assertEqual(setup.KokiriInputs(0xFFFFFFFF, 0, False, 0).prelude("translucent", 0)
                         .bindings[8].packets[1][0] & 0xFFF, 118)

    def test_profile_preserves_disabled_rdp_tlut_and_reference_decode(self):
        entries = fixture()
        # Preserve the source CI4 and palette commands, select the pinned
        # reference's single RGB source, and leave the RDP TLUT disabled.
        opa = entries[OPA].replace(packet(0xEF, 0x8000, 0x30), packet(0xEF, 0, 0x30))
        opa = opa.replace(packet(0xDE, 0, 0x08000001), packet(0xE7))
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "indexed.zip"
            with ZipFile(path, "w") as archive:
                for name, value in {**entries, OPA: opa}.items():
                    archive.writestr(name, value)
            with ZipFile(path) as archive:
                result = trace.DrawTracer(trace.ArchiveResources(archive), ROOT,
                                          profile=setup.KokiriInputs(0, 0, False, 0)).run()
        draw = result["draws"][0]
        palette = draw["textures"][0]["palette"]
        self.assertEqual((palette["bank"], palette["loaded_entries"], palette["raw_tlut"]), (3, 16, 0))
        self.assertIsNone(palette["interpretation"])
        self.assertEqual(palette["reference_interpretation"], "rgba5551")
        self.assertIn("Fast3D CI decode uses RGBA5551 despite disabled RDP TLUT", draw["approximations"])

    def test_bad_context_and_reserved_generated_path(self):
        for args in ((-1, 0, False, 0), (0, 20, False, 0), (0, 0, 2, 0), (0, 0, False, 32768)):
            with self.assertRaises(ValueError):
                setup.KokiriInputs(*args)
        with self.assertRaises(ValueError):
            setup.tile_scroll(((0, 0, 0, 10, 1), (0, 0, 0, 2, 1)), "fixture")
        result = self.trace_lists((packet(0xE7),), (), setup.KokiriInputs(1, 0, False, 0),
                                  **{"@generated/opaque/prelude-0": display_list()})
        self.assertEqual(result["coverage"]["issues"], {"malformed": 1})
        self.assertIn("reserved generated path", result["issues"][0]["reason"])

    def test_generated_segment_requires_a_packet_boundary(self):
        result = self.trace_lists((packet(0xDE, 0, 0x0B000003),), (), setup.KokiriInputs(0, 0, False, 0))
        self.assertEqual(result["coverage"]["issues"], {"malformed": 1})
        self.assertIn("packet boundary", result["issues"][0]["reason"])
        self.assertFalse(result["generated_calls"])

    def test_profile_rejects_a_different_archive_revision(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "different.zip"
            with ZipFile(path, "w") as archive:
                archive.writestr("version", bytes(5))
            with self.assertRaisesRegex(ValueError, "NTSC 1.0"):
                trace.trace_archive(path, setup.KOKIRI_SCENE, profile=setup.KokiriInputs(0, 0, False, 0))

    def test_hyrule_explicit_day_profile_reconstructs_scroll_and_fog_light_inputs(self):
        profile = setup.HyruleFieldInputs(17, 8, True)
        xlu = profile.prelude("translucent", 0)
        self.assertEqual(set(xlu.bindings), {8, 9, 10})
        self.assertEqual(trace.rect(*xlu.bindings[8].packets[1]), [110, 51, 234, 175])
        self.assertEqual(trace.rect(*xlu.bindings[8].packets[3]), [17, 51, 141, 175])
        self.assertEqual(trace.rect(*xlu.bindings[9].packets[1]), [110, 42, 234, 166])
        self.assertEqual(xlu.bindings[10].packets, ((0xDF000000, 0),))
        self.assertEqual(profile.prelude("opaque", 0).bindings, {})

        setting = {"ambient": [92, 92, 92], "light1_direction": [73, 73, 73],
                   "light1_color": [160, 134, 118], "light2_direction": [183, 183, 183],
                   "light2_color": [30, 10, 10], "fog_color": [30, 10, 10],
                   "fog_near": 2010, "fog_far": 12800}
        scene_commands = [{"id": 15, "lighting_settings": [setting] * 9}]
        state = trace.PassState({})
        profile.seed(state, scene_commands)
        self.assertEqual(state.num_directional_lights["count"], 2)
        self.assertEqual(state.lights["1"]["direction"], [-73, -73, -73])
        self.assertEqual(state.lights["2"]["color"], [92, 92, 92])
        self.assertEqual(state.fog_factor["multiplier"], 0)
        self.assertEqual(state.fog_factor["offset_coefficient"], 0)
        self.assertEqual(state.colors["fog"]["rgba"], [30, 10, 10, 0])

    def test_hyrule_fog_range_matches_source_special_cases_and_integer_formula(self):
        cases = ((-1, 12800, (0, 255)), (996, 1000, (32000, -31744)),
                 (997, 1000, (32767, -32512)), (1000, 12800, (0, 0)))
        for near, far, expected in cases:
            actual = setup.fog_factor_for_setting({"fog_near": near, "fog_far": far})
            self.assertEqual((actual["multiplier"], actual["offset_coefficient"]), expected)

    def test_hyrule_context_rejects_night_snapshot_and_bad_selection(self):
        with self.assertRaisesRegex(ValueError, "nighttime segment"):
            setup.HyruleFieldInputs(0, 8, False)
        for args in ((-1, 0, True), (0, 17, True), (0, 0, 1)):
            with self.assertRaises(ValueError):
                setup.HyruleFieldInputs(*args)

    def test_explicit_static_scene_lighting_profile_is_provenanced_and_bounded(self):
        row = {"ambient": [50, 50, 50], "light1_direction": [0, 0, 0],
               "light1_color": [255, 255, 255], "light2_direction": [0, 0, 0],
               "light2_color": [0, 0, 0], "fog_color": [20, 30, 40],
               "fog_near": 2020, "fog_far": 2000}
        profile = setup.StaticSceneLightingInputs("scenes/shared/market_day_scene/market_day_scene",
                                                  0, (128, 128, 128, 128))
        selected = profile.lighting([{"id": 15, "lighting_settings": [row]}])
        self.assertEqual(selected["setting"], row)
        self.assertEqual(selected["provenance"]["origin"], "reconstructed")
        self.assertEqual(selected["fog_factor"]["multiplier"], 0)
        self.assertEqual(selected["fog_factor"]["offset_coefficient"], 0)
        state = trace.PassState({})
        profile.seed(state, [{"id": 15, "lighting_settings": [row]}])
        self.assertEqual(state.num_directional_lights["count"], 2)
        self.assertEqual(state.lights["2"]["color"], [50, 50, 50])
        self.assertEqual(len(profile.prelude("opaque", 0).commands), 2)
        self.assertEqual(len(profile.prelude("translucent", 0).commands), 2)
        self.assertEqual(len(profile.prelude("opaque", 3).commands), 3)
        with self.assertRaisesRegex(ValueError, "lighting table"):
            profile.lighting([{"id": 15, "lighting_settings": [row]}] * 2)
        with self.assertRaises(ValueError):
            setup.StaticSceneLightingInputs("scenes/market", 1, (256, 0, 0, 0))


class MaterialTests(unittest.TestCase):
    def test_effective_combiner_dependency(self):
        c1 = {"rgb": [2, 1, 12, 1], "alpha": [7, 7, 7, 5]}
        c2 = {"rgb": [0, 15, 4, 7], "alpha": [7, 7, 7, 2]}
        first_only = material.material_expression([c1, c2], 0, False)
        self.assertEqual(first_only["inputs"], ["tex0.rgb"])
        both = material.material_expression([c1, c2], 1, True)
        self.assertEqual(both["rgb"], ("multiply", "shade.rgb", ("combine", "tex1.rgb", "tex0.rgb",
                                                                 "environment.a", "tex0.rgb")))
        self.assertEqual(both["alpha"], "tex0.a")
        replaced = material.material_expression([c1, {"rgb": [15, 15, 31, 4], "alpha": [7]*4}], 1, False)
        self.assertEqual(replaced["inputs"], ["shade.rgb"])

    def test_reference_blender_flags(self):
        self.assertFalse(material.reference_blender(0xC8112078)["alpha"])
        self.assertTrue(material.reference_blender(0xC8113078)["alpha_test"])
        self.assertTrue(material.reference_blender(0xC81049D8)["alpha"])
        self.assertTrue(material.reference_blender(0xC81049D8)["fog"])

    def test_authored_bake_ci4_bank_ci8_tlut_and_uv(self):
        bank3 = bytearray(512)
        bank3[96:100] = bytes.fromhex("f80107c0")
        rgba = lowering.bake_indexed(b"\x01\x10", bytes(bank3), width=4, height=1, index_bits=4,
                                     bank=3, interpretation="rgba5551")
        self.assertEqual(rgba.hex(), "ff0000ff00ff000000ff0000ff0000ff")
        bank3[510:512] = bytes.fromhex("7f01")
        rgba = lowering.bake_indexed(b"\xff\x00\xff\x00", bytes(bank3), width=4, height=1,
                                     index_bits=8, bank=0, interpretation="ia88")
        self.assertEqual(rgba.hex(), "7f7f7f01000000007f7f7f0100000000")
        self.assertEqual(lowering.bake_uv(65, 65535, 1, 4), 1)
        self.assertEqual(lowering.bake_uv(-64, 65535, 1, 4), -64)
        self.assertEqual(lowering.scroll_rate([0, 4, 8, 12], 4), -32)
        self.assertIsNone(lowering.scroll_rate([0, 3, 8, 12], 4))
        for args in ((64, 65535, 1, 4), (-32768, 65535, 15, 0), (1, 1, 16, 0)):
            with self.assertRaises(ValueError):
                lowering.bake_uv(*args)
        with self.assertRaisesRegex(ValueError, "incomplete"):
            lowering.bake_indexed(b"\x01\x10", bytes(32), width=4, height=1, index_bits=4,
                                  bank=3, interpretation="rgba5551")

    def test_lowering_verdicts_preserve_original_material(self):
        opaque = {"pass": "opaque", "geometry": {"mode": 1, "known_mask": 0xFFFFFF, "vertex_ranges": []},
                  "combiner": {"effective": {"rgb": "shade.rgb", "alpha": 1,
                                             "inputs": ["shade.rgb"], "texture_inputs": []}, "colors": {}},
                  "render_state": {"reference_blender": material.reference_blender(0x00552078),
                                   "other_mode_low": 0x00552078, "other_mode_high": 0x82C00,
                                   "depth_test": True, "depth_write": True, "z_mode": 0},
                  "textures": [], "missing": [], "approximations": []}
        self.assertEqual(lowering.classify_draw(opaque)["classification"], lowering.EXACT)
        self.assertEqual(lowering.classify_draw({**opaque, "approximations": ["CULLDL"]})["classification"],
                         lowering.APPROXIMATE)
        self.assertEqual(lowering.classify_draw({**opaque, "missing": ["unbound source"]})["classification"],
                         lowering.BLOCKED)
        self.assertEqual(lowering.classify_draw({**opaque, "geometry": {**opaque["geometry"], "mode": 0x401}})
                         ["classification"], lowering.EXTENSION)
        # A 4x4 original CI4 fixture for which the entire bank and upload
        # footprint have been independently validated.
        authored = bytes.fromhex("0110" * 4)
        palette = bytes.fromhex("f80107c0") + bytes(28)
        self.assertEqual(len(lowering.bake_indexed(authored, palette, width=4, height=4,
                                                  index_bits=4, bank=0, interpretation="rgba5551")), 64)
        axis = trace.axis_state({"s_shift": 0, "s_mask": 2, "s_mode": 0}, "s", 0, 12, 4)
        tex = {"input": 0, "descriptor": {"format": 2, "size": 0}, "palette": {"bank": 0},
               "uv": {"s": axis, "t": axis, "tile_extent": [4, 4]},
               "load": {"image": {"source": {"width": 4, "height": 4, "bits_per_pixel": 4,
                                             "horizontal_byte_scale": 1.0, "vertical_pixel_scale": 1.0}},
                        "kind": "block", "source_offset": 0, "total_bytes": 8}}
        ci = {**opaque, "textures": [tex], "combiner": {**opaque["combiner"], "effective":
              {"rgb": ("multiply", "shade.rgb", "tex0.rgb"), "alpha": 1,
               "inputs": ["shade.rgb", "tex0.rgb"], "texture_inputs": [0]}}}
        self.assertEqual(lowering.classify_draw(ci, inherited_filter=2)["classification"], lowering.APPROXIMATE)
        self.assertEqual(lowering.classify_draw(ci, inherited_filter=2, validated_bakes=("palette",))
                         ["classification"], lowering.BAKED)
        before = json.dumps(ci["combiner"], sort_keys=True)
        lowering.lower_report({"draws": [ci]}, inherited_filter=2)
        self.assertEqual(before, json.dumps(ci["combiner"], sort_keys=True))


if __name__ == "__main__":
    unittest.main()
