#!/usr/bin/env python3
"""Original authored command packets and independently checked Fast3D expectations."""

from collections import Counter
import json
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import o2r_draw_trace as trace
from tools.hyrule_importer import crc64_resource_path

ROOT = "studio/scene"
ROOM = "studio/room"
OPA = "studio/opaque"
XLU = "studio/translucent"
VTX = "studio/vertex"
BAKED_VTX = "studio/array"
SCROLL = "studio/scroll"
CI4 = "studio/ci4"
CI8 = "studio/ci8"
PAL4 = "studio/palette-bank"
PAL8 = "studio/palette-full"


def packet(opcode, a=0, b=0):
    return struct.pack(">II", (opcode << 24) | a, b)


def resource(kind, body, version=0):
    return bytes((1, 0, 0, 0)) + struct.pack(">II", int.from_bytes(kind.encode("ascii"), "big"), version) + bytes(52) + body


def ref(opcode, path, a=0, b=0):
    return packet(opcode, a, b) + struct.pack(">Q", crc64_resource_path(path))


def text(value):
    data = value.encode("ascii")
    return struct.pack(">I", len(data)) + data


def scene(*commands):
    return resource("OROM", struct.pack(">I", len(commands) + 1)
                    + b"".join(struct.pack(">I", opcode) + body for opcode, body in commands)
                    + struct.pack(">I", 20))


def display_list(*commands):
    return resource("ODLT", b"\x04" + bytes(7) + b"".join(commands) + packet(0xDF))


def tile(index, tmem, fmt=2, size=0, line=0, bank=0, s=(0, 3, 0), t=(0, 2, 0)):
    # Each axis specifies address mode, mask exponent, shift.
    a = (fmt << 21) | (size << 19) | (line << 9) | tmem
    b = (index << 24) | (bank << 20) | (t[0] << 18) | (t[1] << 14) | (t[2] << 10)
    b |= (s[0] << 8) | (s[1] << 4) | s[2]
    return packet(0xF5, a, b)


def bounds(index, uls=0, ult=0, width=8, height=4):
    return packet(0xF2, (uls << 12) | ult, (index << 24) | ((uls + (width - 1) * 4) << 12) | (ult + (height - 1) * 4))


def texture(fmt, width, height, length):
    return resource("OTEX", struct.pack(">IIII", fmt, width, height, length) + bytes(length))


def base(mode, *, textured=False):
    return (packet(0xD9, 0, 1), packet(0xEF, 0x8000, mode),
            packet(0xFC, 0, ((1 if textured else 4) << 15) | ((2 if textured else 5) << 9)),
            packet(0xFB, 0, 0x80FF407F), ref(0x32, VTX, 0x3006, 16))


def fixture():
    # Palette bank 3 is loaded after the full TLUT. Both units observe the
    # overwrite because the physical entries share the same TLUT.
    return {
        ROOT: scene((4, struct.pack(">I", 1) + text(ROOM) + bytes(8))),
        ROOM: scene((10, bytes((0, 0, 1, 0)) + text(OPA) + text(XLU))),
        VTX: resource("OVTX", struct.pack(">I", 4) + bytes(64)),
        BAKED_VTX: resource("OARR", struct.pack(">II", 25, 3) + bytes(48)),
        CI4: texture(3, 8, 4, 16), CI8: texture(4, 8, 4, 32),
        PAL4: texture(2, 16, 1, 32), PAL8: texture(2, 256, 1, 512),
        SCROLL: display_list(bounds(0, uls=4), packet(0xFB, 0, 0x332211AB), packet(0xD7, 2, 0x01000200)),
        OPA: display_list(
            packet(0xD7, 2, 0xFFFF8000),
            *base(0x30, textured=True),
            ref(0x20, PAL8, (2 << 19) | 255), tile(7, 256, fmt=0, size=2),
            packet(0xF0, 0, (7 << 24) | (255 << 14)),
            ref(0x20, PAL4, (2 << 19) | 15), tile(7, 304, fmt=0, size=2),
            packet(0xF0, 0, (7 << 24) | (15 << 14)),
            ref(0x20, CI4, 2 << 19), tile(7, 0, size=2),
            packet(0xF3, 0, (7 << 24) | (7 << 12) | 0x800),
            ref(0x20, CI8, (1 << 19) | 7), tile(7, 128, size=1),
            packet(0xF3, 0, (7 << 24) | (31 << 12) | 0x400),
            tile(0, 0, line=1, bank=3, s=(2, 3, 1), t=(1, 2, 0)), bounds(0),
            tile(1, 128, size=1, line=1, s=(0, 3, 12)), bounds(1),
            packet(0x05, 0x000204), packet(0xDE, 0, 0x08000001),
            packet(0x06, 0x000204, 0x000402),
        ),
        XLU: display_list(*base(0x4411), ref(0x32, BAKED_VTX, 0x3006),
                          packet(0x05, 0x000204)),
    }


class TraceTests(unittest.TestCase):
    def run_trace(self, entries, segments=None):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "sample.o2r"
            with ZipFile(path, "w") as archive:
                for key, data in entries.items():
                    archive.writestr(key, data)
            before = path.read_bytes()
            result = trace.trace_archive(path, ROOT, segments)
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual([path], list(Path(tmp).iterdir()))
            return result

    def test_reference_checked_draw_state_and_order(self):
        # Expected values independently worked from the pinned interpreter's
        # GfxDpLoadTlut, GfxDpSetTile, GfxSpTri1, and F3DEX2 vertex handlers.
        entries = fixture()
        segments = {8: trace.Reference(SCROLL)}
        report = self.run_trace(entries, segments)
        first, after_scroll, translucent = report["draws"]
        self.assertEqual(report["coverage"]["draws"], {"complete": 3})
        self.assertEqual(report["issues"], [])
        self.assertEqual([e["pass"] for e in report["draws"]], ["opaque", "opaque", "translucent"])
        self.assertEqual([e["order"] for e in report["draws"]], [0, 1, 2])
        self.assertEqual(first["geometry"]["vertex_ranges"],
                         [{"resource": VTX, "first": 1, "last": 3, "referenced_count": 3,
                           "uv_scale_u0_16_at_load": (65535, 32768), "geometry_at_vertex_load": 0}])
        self.assertEqual(after_scroll["geometry"]["vertex_ranges"], first["geometry"]["vertex_ranges"])
        self.assertEqual(translucent["geometry"]["vertex_ranges"][0]["resource"], BAKED_VTX)
        self.assertEqual(after_scroll["primitive_count"], 2)
        self.assertEqual(first["combiner"]["mux_texture_inputs"], [0, 1])
        self.assertEqual(first["combiner"]["selectors"][0]["rgb"][-1], 1)
        self.assertEqual(first["combiner"]["selectors"][0]["alpha"][-1], 2)
        ci4, ci8 = first["textures"]
        self.assertEqual(ci4["palette"]["bank"], 3)
        self.assertEqual(ci4["palette"]["first_entry"], 48)
        self.assertEqual(ci4["palette"]["loaded_entries"], 16)
        self.assertEqual(ci4["palette"]["sources"][0]["path"], PAL4)
        self.assertEqual(ci8["palette"]["loaded_entries"], 256)
        self.assertEqual({load["path"] for load in ci8["palette"]["sources"]}, {PAL4, PAL8})
        self.assertEqual(ci4["palette"]["interpretation"], "rgba5551")
        self.assertEqual(ci4["load"]["total_bytes"], 16)
        self.assertEqual(ci8["load"]["source_stride_bytes"], 8)
        self.assertEqual(ci4["uv"]["s"]["shift_multiplier"], 0.5)
        self.assertEqual(ci8["uv"]["s"]["shift_multiplier"], 16)
        self.assertEqual(ci4["uv"]["s"]["mask_period_texels"], 8)
        self.assertTrue(ci4["uv"]["s"]["clamp"])
        self.assertTrue(ci4["uv"]["t"]["mirror"])
        self.assertEqual((ci4["uv"]["s"]["subtract_origin_texels"],
                          after_scroll["textures"][0]["uv"]["s"]["subtract_origin_texels"]), (0, 1))
        self.assertEqual(after_scroll["textures"][0]["descriptor"]["bounds_source"]["display_list"], SCROLL)
        self.assertEqual(first["combiner"]["colors"]["environment"]["rgba"], [128, 255, 64, 127])
        self.assertEqual(after_scroll["combiner"]["colors"]["environment"]["rgba"], [51, 34, 17, 171])
        self.assertTrue(first["render_state"]["depth_test"])
        self.assertTrue(first["render_state"]["depth_write"])
        self.assertTrue(translucent["render_state"]["depth_test"])
        self.assertFalse(translucent["render_state"]["depth_write"])
        self.assertEqual(translucent["render_state"]["alpha_compare"], 1)
        self.assertTrue(translucent["render_state"]["force_blend"])
        self.assertIn("independent texture inputs", first["wsc2_gaps"])
        self.assertIn("environment color is distinct from vertex tint", translucent["wsc2_gaps"])
        self.assertNotIn("primitive color or LOD constants", translucent["wsc2_gaps"])
        reverse = self.run_trace(dict(reversed(list(entries.items()))), segments)
        self.assertEqual(json.dumps(report, sort_keys=True), json.dumps(reverse, sort_keys=True))

    def test_render_and_repack_data_are_absent(self):
        report = self.run_trace(fixture(), {8: trace.Reference(SCROLL)})
        for draw in report["draws"]:
            self.assertNotIn("triangles", draw)
            self.assertNotIn("vertices", draw["geometry"])
            for tex in draw["textures"]:
                self.assertNotIn("pixels", tex)
                self.assertNotIn("payload", tex["load"]["image"]["source"])

    def test_failure_categories_and_taint(self):
        report = self.run_trace(fixture())
        self.assertEqual(report["coverage"]["draws"], {"complete": 2, "incomplete": 1})
        self.assertEqual(report["coverage"]["issues"], {"unresolved": 1})
        self.assertEqual(report["issues"][0]["command"], "DL")
        self.assertIn("unbound segment 0x08", report["issues"][0]["reason"])
        for data, status in ((display_list(ref(0x32, CI4, 0x3006)), "malformed"),
                             (display_list(ref(0x32, VTX, 0x3006, 64)), "malformed"),
                             (resource("ODLT", b"\x04" + bytes(7) + packet(0x31)), "malformed"),
                             (resource("ODLT", b"\x04" + bytes(7) + packet(0x00)), "malformed"),
                             (display_list(packet(0x04)), "unsupported"),
                             (display_list(packet(0x0E)), "unsupported"),
                             (resource("ODLT", b"\x05"), "unsupported")):
            with self.subTest(status=status, payload=data):
                result = self.run_trace({**fixture(), OPA: data})
                self.assertGreater(result["coverage"]["issues"].get(status, 0), 0)
                self.assertEqual({d["pass"] for d in result["draws"]}, {"translucent"})

    def test_call_cycle_and_typed_segments(self):
        result = self.run_trace({**fixture(), OPA: display_list(ref(0x31, OPA))})
        self.assertEqual(Counter(e["status"] for e in result["issues"]), {"malformed": 1})
        self.assertIn("call cycle", result["issues"][0]["reason"])
        entries = {**fixture(), OPA: display_list(packet(0xDB, 0x060024, 0x08000001),
                   packet(0xDE, 0x010000, 0x09000009), packet(0x0E)),
                   SCROLL: display_list(packet(0x00), *base(0x30), packet(0x05, 0x000204))}
        result = self.run_trace(entries, {8: trace.Reference(SCROLL)})
        self.assertEqual(result["issues"], [])
        self.assertEqual(result["draws"][0]["display_list"], SCROLL)
        result = self.run_trace(entries, {8: trace.Reference(SCROLL, 2)})
        self.assertIn("packet boundary", result["issues"][0]["reason"])

    def test_unknown_render_bits_and_cull_are_explicit(self):
        entries = {**fixture(), OPA: display_list(ref(0x32, VTX, 0x3006), packet(0x03),
                                                 packet(0xFC), packet(0x05, 0x000204))}
        result = self.run_trace(entries)
        draw = result["draws"][0]
        self.assertEqual(draw["status"], "incomplete")
        self.assertIsNone(draw["render_state"]["depth_test"])
        self.assertIsNone(draw["render_state"]["depth_write"])
        self.assertIsNone(draw["combiner"]["cycle_type"])
        self.assertIn("CULLDL falls through as in the reference; visibility not evaluated", draw["approximations"])

    def test_budgets_and_invalid_texture_payload(self):
        with patch.object(trace, "MAX_COMMANDS", 2):
            self.assertEqual(self.run_trace(fixture())["coverage"]["issues"], {"limit": 1})
        with patch.object(trace, "MAX_LIST_COMMANDS", 4):
            self.assertIsNotNone(self.run_trace(fixture())["coverage"]["budget_error"])
        with patch.object(trace, "MAX_READ_BYTES", 8):
            self.assertIsNotNone(self.run_trace(fixture())["coverage"]["budget_error"])
        with patch.object(trace, "MAX_CALL_DEPTH", 1):
            self.assertEqual(self.run_trace(fixture(), {8: trace.Reference(SCROLL)})["coverage"]["issues"], {"limit": 1})
        result = self.run_trace({**fixture(), CI4: texture(3, 8, 4, 15)}, {8: trace.Reference(SCROLL)})
        self.assertIn("texture load exceeds the recorded payload", [e["reason"] for e in result["issues"]])

    def test_partial_tlut_and_shader_requirements(self):
        short_palette = fixture()
        short_palette[OPA] = short_palette[OPA].replace(packet(0xF0, 0, (7 << 24) | (255 << 14)),
                                                       packet(0xF0, 0, (7 << 24) | (127 << 14)))
        result = self.run_trace(short_palette, {8: trace.Reference(SCROLL)})
        self.assertIn("tile 1 palette entries missing (128/256)", result["draws"][0]["missing"])
        self.assertEqual(result["draws"][0]["textures"][1]["palette"]["loaded_entries"], 128)

    def test_segmented_texture_tile_load_and_partial_other_mode(self):
        rgba = "studio/rgba"
        entries = {**fixture(), rgba: texture(2, 8, 4, 64), OPA: display_list(
            packet(0xD7, 2, 0xFFFFFFFF), *base(0x30),
            packet(0xFC, 0, (1 << 15) | (4 << 9)),
            packet(0xE2, (26 << 8), 0),  # clear Z_UPD, preserve Z_CMP
            packet(0xE3, (18 << 8) | 1, 2 << 12),  # linear filter bits only
            packet(0xFD, (2 << 19) | 7, 0x0A000001),
            tile(7, 0, fmt=0, size=2), packet(0xF4, (8 << 12) | 4, (7 << 24) | (20 << 12) | 8),
            tile(0, 0, fmt=0, size=2), bounds(0, width=4, height=2),
            packet(0x05, 0x000204),
        )}
        report = self.run_trace(entries, {10: trace.Reference(rgba)})
        self.assertEqual(report["issues"], [])
        draw = report["draws"][0]
        self.assertEqual(draw["status"], "approximate")
        load = draw["textures"][0]["load"]
        self.assertEqual((load["source_offset"], load["source_stride_bytes"], load["row_bytes"]), (20, 16, 8))
        self.assertEqual((load["width"], load["height"]), (4, 2))
        self.assertEqual(draw["textures"][0]["uv"]["half_texel_bias"], 0.5)
        self.assertTrue(draw["render_state"]["depth_test"])
        self.assertFalse(draw["render_state"]["depth_write"])

    def test_cli_cannot_overwrite_archive(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "sample.o2r"
            with ZipFile(path, "w") as archive:
                for key, value in fixture().items():
                    archive.writestr(key, value)
            before = path.read_bytes()
            result = subprocess.run([sys.executable, Path(trace.__file__), path, ROOT, "--output", path],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("separate from the input archive", result.stderr)
            self.assertEqual(path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
