#!/usr/bin/env python3
"""Inspect Shipwright O2R archives and convert supported OoT world resources.

The tool deliberately emits an intermediate package rather than executable/game
assets. It never writes into the repository unless the caller chooses that path.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
import zipfile
from pathlib import Path, PurePosixPath


FORMAT_NAME = "sm64-hyrule-world"
FORMAT_VERSION = 1
OTR_HEADER_SIZE = 64
MAX_COUNT = 2_000_000
MAX_STRING = 1_048_576

RESOURCE_TYPES = {
    0x4F444C54: "DisplayList",
    0x4F4D5458: "Matrix",
    0x4F544558: "Texture",
    0x4F565458: "Vertex",
    0x4F415252: "Array",
    0x4F414E4D: "Animation",
    0x4F50414D: "PlayerAnimation",
    0x4F524F4D: "Scene",
    0x4F434F4C: "CollisionHeader",
    0x4F534B4C: "Skeleton",
    0x4F534C42: "SkeletonLimb",
    0x4F505448: "Path",
    0x4F435654: "Cutscene",
    0x4F545854: "Text",
    0x4F415544: "Audio",
    0x4F534D50: "AudioSample",
    0x4F534654: "AudioSoundFont",
    0x4F534551: "AudioSequence",
    0x4F424749: "Background",
    0x4F52434D: "SceneCommand",
    0x4F544558: "Texture",
    0x4F424C42: "Blob",
}

ARRAY_TYPES = {24: "Vector", 25: "Vertex"}
TEXTURE_TYPES = {
    1: ("RGBA32bpp", 32), 2: ("RGBA16bpp", 16), 3: ("Palette4bpp", 4),
    4: ("Palette8bpp", 8), 5: ("Grayscale4bpp", 4), 6: ("Grayscale8bpp", 8),
    7: ("GrayscaleAlpha4bpp", 4), 8: ("GrayscaleAlpha8bpp", 8),
    9: ("GrayscaleAlpha16bpp", 16),
}

# Shipwright's binary ODLT resources use F3DEX2 plus a small set of widened
# commands for references to O2R resources. The extra 64 bits carry the CRC64
# of the referenced archive path; they are not an address or a texture payload.
DISPLAY_LIST_WIDE_OPCODES = {0x20, 0x31, 0x32, 0x33, 0x35, 0x36, 0x42}
F3DEX2_END_OPCODE = 0xDF
DISPLAY_LIST_OPCODE_NAMES = {
    0x00: "SPNOOP", 0x01: "VTX", 0x02: "MODIFYVTX", 0x03: "CULLDL",
    0x04: "BRANCH_Z", 0x05: "TRI1", 0x06: "TRI2", 0x07: "QUAD",
    0x08: "LINE3D", 0x20: "SETTIMG_OTR_HASH", 0x31: "DL_OTR_HASH",
    0x32: "VTX_OTR_HASH", 0x33: "MARKER", 0x35: "BRANCH_Z_OTR",
    0x36: "MTX_OTR", 0x42: "MOVEMEM_OTR", 0xD7: "TEXTURE",
    0xD8: "POPMTX", 0xD9: "GEOMETRYMODE", 0xDA: "MTX",
    0xDB: "MOVEWORD", 0xDC: "MOVEMEM", 0xDD: "LOAD_UCODE",
    0xDE: "DL", 0xDF: "ENDDL", 0xE0: "RDPHALF_2",
    0xE1: "SETOTHERMODE_L", 0xE2: "SETOTHERMODE_L",
    0xE3: "SETOTHERMODE_H", 0xE4: "TEXRECT", 0xE5: "TEXRECTFLIP",
    0xE6: "LOADSYNC", 0xE7: "PIPESYNC", 0xE8: "TILESYNC",
    0xE9: "FULLSYNC", 0xEA: "SETKEYGB", 0xEB: "SETKEYR",
    0xEC: "SETCONVERT", 0xED: "SCISSOR", 0xEE: "PRIMDEPTH",
    0xEF: "RDPSETOTHERMODE", 0xF0: "LOADTLUT", 0xF1: "RDPHALF_1",
    0xF2: "SETTILESIZE", 0xF3: "LOADBLOCK", 0xF4: "LOADTILE",
    0xF5: "SETTILE", 0xF6: "FILLRECT", 0xF7: "FILLZIMG",
    0xF8: "SETFILLCOLOR", 0xF9: "SETFOGCOLOR", 0xFA: "SETPRIMCOLOR",
    0xFB: "SETENVCOLOR", 0xFC: "SETCOMBINE", 0xFD: "SETTIMG",
    0xFE: "SETZIMG", 0xFF: "SETCIMG",
}

TEXTURE_FORMAT_NAMES = {
    1: "RGBA32bpp", 2: "RGBA16bpp", 3: "Palette4bpp",
    4: "Palette8bpp", 5: "Grayscale4bpp", 6: "Grayscale8bpp",
    7: "GrayscaleAlpha4bpp", 8: "GrayscaleAlpha8bpp",
    9: "GrayscaleAlpha16bpp",
}

COMMAND_NAMES = {
    0: "SetStartPositionList", 1: "SetActorList", 2: "SetCsCamera",
    3: "SetCollisionHeader", 4: "SetRoomList", 5: "SetWind",
    6: "SetEntranceList", 7: "SetSpecialObjects", 8: "SetRoomBehavior",
    9: "Unused09", 10: "SetMesh", 11: "SetObjectList", 12: "SetLightList",
    13: "SetPathways", 14: "SetTransitionActorList", 15: "SetLightingSettings",
    16: "SetTimeSettings", 17: "SetSkyboxSettings", 18: "SetSkyboxModifier",
    19: "SetExitList", 20: "EndMarker", 21: "SetSoundSettings",
    22: "SetEchoSettings", 23: "SetCutscenes", 24: "SetAlternateHeaders",
    25: "SetCameraSettings",
}


class ImportFailure(Exception):
    pass


class Reader:
    def __init__(self, data: bytes, endian: str, label: str):
        self.data = data
        self.endian = endian
        self.label = label
        self.offset = 0

    def take(self, size: int) -> bytes:
        if size < 0 or self.offset + size > len(self.data):
            raise ImportFailure(f"{self.label}: truncated at byte {self.offset}, need {size} bytes")
        result = self.data[self.offset:self.offset + size]
        self.offset += size
        return result

    def unpack(self, fmt: str):
        size = struct.calcsize(fmt)
        return struct.unpack(self.endian + fmt, self.take(size))

    def i8(self) -> int:
        return self.unpack("b")[0]

    def u8(self) -> int:
        return self.unpack("B")[0]

    def i16(self) -> int:
        return self.unpack("h")[0]

    def u16(self) -> int:
        return self.unpack("H")[0]

    def i32(self) -> int:
        return self.unpack("i")[0]

    def u32(self) -> int:
        return self.unpack("I")[0]

    def count(self, label: str, maximum: int = MAX_COUNT) -> int:
        value = self.u32()
        if value > maximum:
            raise ImportFailure(f"{self.label}: unreasonable {label} count {value}")
        return value

    def string(self) -> str:
        size = self.i32()
        if size < 0 or size > MAX_STRING:
            raise ImportFailure(f"{self.label}: invalid length-prefixed string size {size}")
        try:
            return self.take(size).decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ImportFailure(f"{self.label}: invalid UTF-8 resource path") from exc


def safe_archive_path(name: str) -> str:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or "\x00" in name:
        raise ImportFailure(f"unsafe archive path: {name!r}")
    return str(path)


def read_resource_header(data: bytes, name: str) -> dict:
    if len(data) < OTR_HEADER_SIZE:
        return {"status": "headerless-or-short", "resource_type": None, "version": None}
    byte_order = data[0]
    if byte_order not in (0, 1):
        return {"status": "unrecognized-header", "resource_type": None, "version": None}
    endian = "<" if byte_order == 0 else ">"
    resource_type, version = struct.unpack_from(endian + "II", data, 4)
    resource_id = struct.unpack_from(endian + "Q", data, 12)[0]
    return {
        "status": "ok",
        "endian": "little" if byte_order == 0 else "big",
        "custom": bool(data[1]),
        "resource_type_code": f"0x{resource_type:08x}",
        "resource_type": RESOURCE_TYPES.get(resource_type, "unknown"),
        "version": version,
        "resource_id": f"0x{resource_id:016x}",
        "body_offset": OTR_HEADER_SIZE,
    }


def parse_collision(body: bytes, endian: str, path: str) -> dict:
    reader = Reader(body, endian, path)
    bounds = [reader.i16() for _ in range(6)]
    vertex_count = reader.i32()
    if vertex_count < 0 or vertex_count > 1_000_000:
        raise ImportFailure(f"{path}: invalid collision vertex count {vertex_count}")
    vertices = [[reader.i16(), reader.i16(), reader.i16()] for _ in range(vertex_count)]
    polygon_count = reader.count("collision polygon", 2_000_000)
    polygons = []
    for poly_index in range(polygon_count):
        surface_type = reader.u16()
        packed_a = reader.u16()
        packed_b = reader.u16()
        packed_c = reader.u16()
        indices = [packed_a & 0x1FFF, packed_b & 0x1FFF, packed_c & 0x1FFF]
        normal = [reader.i16(), reader.i16(), reader.i16()]
        distance = reader.i16()
        if any(index >= vertex_count for index in indices):
            raise ImportFailure(f"{path}: polygon {poly_index} references a missing vertex {indices}")
        polygons.append({"surface_type": surface_type, "vertex_indices": indices,
                         "vertex_flags": [packed_a >> 13, packed_b >> 13],
                         "normal": normal, "distance": distance})

    surface_count = reader.count("surface type", 65536)
    surface_types = []
    for _ in range(surface_count):
        data1 = reader.u32()
        data0 = reader.u32()
        surface_types.append([data0, data1])

    camera_count = reader.count("camera", 65536)
    camera_data = []
    for _ in range(camera_count):
        camera_data.append({"camera_type": reader.u16(), "num_cameras": reader.i16(),
                            "position_index": reader.i32()})
    camera_position_count = reader.i32()
    if camera_position_count < 0 or camera_position_count > 1_000_000:
        raise ImportFailure(f"{path}: invalid camera-position count {camera_position_count}")
    camera_positions = [[reader.i16(), reader.i16(), reader.i16()] for _ in range(camera_position_count)]

    water_count = reader.i32()
    if water_count < 0 or water_count > 65536:
        raise ImportFailure(f"{path}: invalid water-box count {water_count}")
    water_boxes = []
    for _ in range(water_count):
        water_boxes.append({"x_min": reader.i16(), "surface_y": reader.i16(),
                            "z_min": reader.i16(), "x_length": reader.i16(),
                            "z_length": reader.i16(), "properties": reader.i32()})

    if reader.offset < len(body) and any(body[reader.offset:]):
        raise ImportFailure(f"{path}: unexpected non-zero trailing bytes at {reader.offset}")
    return {"bounds": {"min": bounds[:3], "max": bounds[3:]}, "vertices": vertices,
            "polygons": polygons, "surface_types": surface_types,
            "camera_data": camera_data, "camera_positions": camera_positions,
            "water_boxes": water_boxes, "consumed_bytes": reader.offset}


def parse_vertex_array(body: bytes, endian: str, path: str) -> dict:
    reader = Reader(body, endian, path)
    array_type = reader.u32()
    count = reader.count("array element", 2_000_000)
    if array_type != 25:
        raise ImportFailure(f"{path}: array type {array_type} ({ARRAY_TYPES.get(array_type, 'unknown')}) "
                            "is not a vertex array")
    if len(body) - reader.offset != count * 16:
        raise ImportFailure(f"{path}: vertex array declares {count} elements but has "
                            f"{len(body) - reader.offset} payload bytes (expected {count * 16})")
    vertices = []
    for _ in range(count):
        position = [reader.i16(), reader.i16(), reader.i16()]
        flag = reader.u16()
        uv = [reader.i16(), reader.i16()]
        cn_bytes = list(reader.take(4))
        vertices.append({"position": position, "flag": flag,
                         "uv_fixed_s10_5": uv, "uv_texels": [uv[0] / 32.0, uv[1] / 32.0],
                         "cn_bytes": cn_bytes})
    return {"array_type": array_type, "array_type_name": "Vertex", "count": count,
            "vertices": vertices, "consumed_bytes": reader.offset}


def parse_texture_descriptor(body: bytes, endian: str, version: int, path: str) -> dict:
    reader = Reader(body, endian, path)
    texture_type = reader.u32()
    width = reader.u32()
    height = reader.u32()
    if width == 0 or height == 0 or width > 32768 or height > 32768:
        raise ImportFailure(f"{path}: invalid texture dimensions {width}x{height}")
    if version == 0:
        flags, h_byte_scale, v_pixel_scale = 0, 1.0, 1.0
    elif version == 1:
        flags = reader.u32()
        h_byte_scale = reader.unpack("f")[0]
        v_pixel_scale = reader.unpack("f")[0]
    else:
        raise ImportFailure(f"{path}: texture resource version {version} is not supported (need 0 or 1)")
    image_data_size = reader.u32()
    payload_size = len(body) - reader.offset
    if image_data_size != payload_size:
        raise ImportFailure(f"{path}: texture declares {image_data_size} payload bytes but has {payload_size}")
    format_name, bits_per_pixel = TEXTURE_TYPES.get(texture_type, (f"unknown-{texture_type}", 0))
    expected_size = ((width * height * bits_per_pixel + 7) // 8) if bits_per_pixel else None
    return {"format_code": texture_type, "format": format_name, "bits_per_pixel": bits_per_pixel or None,
            "width": width, "height": height, "flags": flags,
            "horizontal_byte_scale": h_byte_scale, "vertical_pixel_scale": v_pixel_scale,
            "image_data_size": image_data_size, "payload_size": payload_size,
            "payload_offset": reader.offset,
            "expected_packed_size": expected_size,
            "expected_packed_size_matches": expected_size == payload_size if expected_size is not None else None,
            "payload_decoded": False}


def crc64_resource_path(path: str) -> int:
    """Return libultraship's CRC64/ECMA resource-path key (no final XOR)."""
    # Resource paths are stored as UTF-8 strings by the archive loader; hash
    # the same bytes rather than restricting otherwise valid localized paths.
    encoded = path.encode("utf-8")
    crc = 0xFFFFFFFFFFFFFFFF
    polynomial = 0x42F0E1EBA9EA3693
    for byte in encoded:
        crc ^= byte << 56
        for _ in range(8):
            crc = (((crc << 1) ^ polynomial) if crc & 0x8000000000000000
                   else (crc << 1)) & 0xFFFFFFFFFFFFFFFF
    return crc


def _f3dex2_triangle_indices(w0: int, w1: int, opcode: int) -> list[list[int]]:
    def decode(word: int, shift: int) -> int:
        return ((word >> shift) & 0xFF) // 2
    first = [decode(w0, 16), decode(w0, 8), decode(w0, 0)]
    if opcode == 0x05:
        return [first]
    return [first, [decode(w1, 16), decode(w1, 8), decode(w1, 0)]]


def parse_display_list(body: bytes, endian: str, path: str) -> dict:
    """Decode a binary ODLT command stream without executing its graphics state."""
    if not body:
        raise ImportFailure(f"{path}: empty display-list body")
    reader = Reader(body, endian, path)
    ucode = reader.u8()
    if ucode not in (0, 1, 2, 3, 4, 5):
        raise ImportFailure(f"{path}: unsupported display-list microcode {ucode}")
    # The ODLT header is one byte; commands begin at the next 8-byte boundary.
    instruction_start = (reader.offset + 7) & ~7
    if instruction_start > len(body):
        raise ImportFailure(f"{path}: truncated alignment padding")
    reader.offset = instruction_start
    end_opcode = 0xB8 if ucode in (0, 1, 2, 3) else F3DEX2_END_OPCODE
    instructions = []
    terminated = False
    while reader.offset < len(body):
        command_offset = reader.offset
        if len(body) - command_offset < 8:
            raise ImportFailure(f"{path}: trailing {len(body) - command_offset} bytes after command stream")
        w0, w1 = reader.unpack("II")
        opcode = (w0 >> 24) & 0xFF
        wide = opcode in DISPLAY_LIST_WIDE_OPCODES
        extension_words = list(reader.unpack("II")) if wide else None
        instruction = {"offset": command_offset, "opcode": opcode,
                       "name": DISPLAY_LIST_OPCODE_NAMES.get(opcode, f"UNKNOWN_{opcode:02X}"),
                       "width_bytes": 16 if wide else 8, "w0": w0, "w1": w1}
        if extension_words is not None:
            instruction["extension_words"] = extension_words
            instruction["resource_hash"] = f"0x{((extension_words[0] << 32) | extension_words[1]):016x}"
        if opcode == 0x32:
            count = (w0 >> 12) & 0xFF
            destination = ((w0 >> 1) & 0x7F) - count
            instruction["vertex_count"] = count
            instruction["vertex_destination"] = destination
            instruction["vertex_array_byte_offset"] = w1
        elif opcode in (0x05, 0x06):
            instruction["triangles"] = _f3dex2_triangle_indices(w0, w1, opcode)
        elif opcode == 0x20:
            instruction["image_format"] = (w0 >> 21) & 7
            instruction["image_size_bits"] = 4 << ((w0 >> 19) & 3)
            instruction["image_width"] = (w0 & 0x3FF) + 1
        elif opcode == 0xF5:
            instruction["tile"] = (w1 >> 24) & 7
            instruction["tile_state"] = {
                "format": (w0 >> 21) & 7, "size": (w0 >> 19) & 3,
                "line_words": (w0 >> 9) & 0x1FF, "tmem_words": w0 & 0x1FF,
                "palette": (w1 >> 20) & 0xF,
                "t_mode": (w1 >> 18) & 3, "t_mask": (w1 >> 14) & 0xF,
                "t_shift": (w1 >> 10) & 0xF, "s_mode": (w1 >> 8) & 3,
                "s_mask": (w1 >> 4) & 0xF, "s_shift": w1 & 0xF,
            }
        elif opcode == 0xF2:
            instruction["tile"] = (w1 >> 24) & 7
            instruction["tile_rect_10_2"] = {
                "uls": (w0 >> 12) & 0xFFF, "ult": w0 & 0xFFF,
                "lrs": (w1 >> 12) & 0xFFF, "lrt": w1 & 0xFFF,
            }
        elif opcode == 0xD7:
            instruction["texture_state"] = {
                "s_scale": (w1 >> 16) & 0xFFFF, "t_scale": w1 & 0xFFFF,
                "level": (w0 >> 11) & 7, "tile": (w0 >> 8) & 7,
                # F3DEX2 gsSPTexture stores the enable bit at bit 1.
                "enabled": bool(w0 & 2),
            }
        elif opcode == 0xD9:
            instruction["geometry_clear_mask"] = (~w0) & 0x00FFFFFF
            instruction["geometry_set_mask"] = w1
        elif opcode in (0xE2, 0xE3):
            instruction["other_mode"] = {"shift": (w0 >> 8) & 0xFF,
                                          "length_minus_one": w0 & 0xFF,
                                          "value": w1}
        if opcode == end_opcode:
            terminated = True
            break
        instructions.append(instruction)
    if not terminated:
        raise ImportFailure(f"{path}: display list has no end opcode 0x{end_opcode:02x}")
    trailing = len(body) - reader.offset
    return {"ucode": ucode, "instruction_start": instruction_start,
            "instructions": instructions, "instruction_count": len(instructions) + 1,
            "triangle_count": sum(len(item.get("triangles", [])) for item in instructions),
            "terminated": terminated, "end_offset": reader.offset - 8,
            "trailing_bytes": trailing}


def decode_texture_rgba(descriptor: dict, payload: bytes,
                        palette_rgba: list[list[int]] | None = None) -> bytes:
    """Decode common O2R/N64 texel formats to tightly packed RGBA8 bytes."""
    width, height = descriptor["width"], descriptor["height"]
    fmt = descriptor["format_code"]
    pixels = width * height
    if len(payload) != descriptor["image_data_size"]:
        raise ImportFailure(f"texture payload length mismatch: expected {descriptor['image_data_size']}, got {len(payload)}")

    def expand5(value: int) -> int:
        return (value << 3) | (value >> 2)

    def nibble(index: int) -> int:
        packed = payload[index >> 1]
        return ((packed >> 4) & 0xF) if index % 2 == 0 else (packed & 0xF)

    rgba = bytearray(pixels * 4)
    if fmt == 1:
        if len(payload) != pixels * 4:
            raise ImportFailure("RGBA32 texture has an unexpected payload size")
        return payload
    if fmt == 2:
        for i in range(pixels):
            value = (payload[i * 2] << 8) | payload[i * 2 + 1]
            rgba[i * 4:i * 4 + 4] = bytes((expand5((value >> 11) & 31),
                                           expand5((value >> 6) & 31),
                                           expand5((value >> 1) & 31),
                                           255 if value & 1 else 0))
        return bytes(rgba)
    if fmt in (3, 4):
        if not palette_rgba:
            raise ImportFailure(f"indexed texture format {TEXTURE_FORMAT_NAMES[fmt]} needs a decoded TLUT palette")
        for i in range(pixels):
            index = nibble(i) if fmt == 3 else payload[i]
            if index >= len(palette_rgba):
                raise ImportFailure(f"palette index {index} exceeds {len(palette_rgba)} entries")
            rgba[i * 4:i * 4 + 4] = bytes(palette_rgba[index])
        return bytes(rgba)
    if fmt in (5, 6, 7, 8, 9):
        for i in range(pixels):
            if fmt == 5:
                intensity, alpha = nibble(i) * 17, 255
            elif fmt == 6:
                intensity, alpha = payload[i], 255
            elif fmt == 7:
                value = nibble(i)
                intensity, alpha = ((value >> 1) & 7) * 255 // 7, (value & 1) * 255
            elif fmt == 8:
                value = payload[i]
                intensity, alpha = ((value >> 4) & 0xF) * 17, (value & 0xF) * 17
            else:
                intensity, alpha = payload[i * 2], payload[i * 2 + 1]
            rgba[i * 4:i * 4 + 4] = bytes((intensity, intensity, intensity, alpha))
        return bytes(rgba)
    raise ImportFailure(f"unsupported O2R texture format code {fmt}")


def decode_room_render_data(room_meshes: list[dict], decoded_scenes: dict,
                            decoded_resources: dict, texture_payloads: dict,
                            display_list_bodies: dict, archive_paths: list[str],
                            endian_by_path: dict, scale: float) -> dict:
    """Resolve and replay the selected room's F3DEX2 scene lists for diagnostics.

    This is an offline decoder, not a second game renderer: it preserves the
    source vertex/material state for later lowering into the game's Gfx path.
    """
    hash_paths: dict[int, list[str]] = {}
    for path in archive_paths:
        key = crc64_resource_path(path)
        hash_paths.setdefault(key, []).append(path)

    draw_calls = []
    display_list_paths = []
    referenced_vertex_arrays = set()
    referenced_textures = set()
    referenced_display_lists = set()
    unresolved_references = []
    unsupported_segmented_calls = []
    marker_mismatches = []
    pending_lists = []

    for mesh_record in room_meshes:
        mesh = mesh_record["mesh"]
        for polygon_index, polygon in enumerate(mesh.get("polygons", [])):
            for render_pass, key in (("opaque", "opaque_display_list"),
                                     ("translucent", "translucent_display_list")):
                path = polygon.get(key)
                if not path:
                    continue
                pending_lists.append({"path": path, "polygon_index": polygon_index,
                                      "render_pass": render_pass,
                                      "position": polygon.get("position", [0, 0, 0]),
                                      "mesh_type": mesh.get("type")})

    parsed_lists = {}
    for draw in pending_lists:
        queue = [draw["path"]]
        seen = set()
        while queue:
            path = queue.pop()
            if path in seen or path in parsed_lists:
                continue
            seen.add(path)
            raw = display_list_bodies.get(path)
            if raw is None:
                unresolved_references.append({"kind": "display-list", "path": path,
                                              "reason": "mesh references a missing or unsupported ODLT resource"})
                continue
            endian = endian_by_path[path]
            parsed = parse_display_list(raw["body"], endian, path)
            parsed_lists[path] = parsed
            display_list_paths.append(path)
            for instruction in parsed["instructions"]:
                opcode = instruction["opcode"]
                if opcode == 0x33:
                    marker_hash = int(instruction["resource_hash"], 16)
                    if marker_hash != crc64_resource_path(path):
                        marker_mismatches.append({"path": path,
                                                  "marker_hash": instruction["resource_hash"],
                                                  "expected_hash": f"0x{crc64_resource_path(path):016x}"})
                if opcode in (0x20, 0x31, 0x32, 0x35, 0x36, 0x42):
                    resource_hash = int(instruction["resource_hash"], 16)
                    paths = hash_paths.get(resource_hash, [])
                    if len(paths) != 1:
                        kind = {0x20: "texture", 0x31: "display-list", 0x32: "vertex-array",
                                0x35: "branch-target", 0x36: "matrix", 0x42: "move-memory"}[opcode]
                        reason = "unresolved CRC64 path hash" if not paths else "CRC64 path-hash collision"
                        unresolved_references.append({"kind": kind, "source": path,
                                                      "hash": instruction["resource_hash"],
                                                      "reason": reason, "candidates": paths[:8]})
                        continue
                    target = paths[0]
                    instruction["resolved_path"] = target
                    target_type = None
                    if opcode == 0x20:
                        target_type = "Texture"
                        referenced_textures.add(target)
                    elif opcode == 0x31:
                        target_type = "DisplayList"
                        referenced_display_lists.add(target)
                        if target not in parsed_lists:
                            queue.append(target)
                    elif opcode == 0x32:
                        target_type = "Array"
                        referenced_vertex_arrays.add(target)
                    if target_type and target not in decoded_resources and target not in texture_payloads \
                            and target not in display_list_bodies:
                        unresolved_references.append({"kind": target_type, "source": path,
                                                      "target": target,
                                                      "reason": "referenced resource is outside supported decoded set"})
                elif opcode == 0xDE:
                    # These are retained as explicit unsupported calls below;
                    # they are segmented pointers, not CRC64 references.
                    unsupported_segmented_calls.append({"source": path,
                                                         "offset": instruction["offset"],
                                                         "w0": f"0x{instruction['w0']:08x}",
                                                         "w1": f"0x{instruction['w1']:08x}"})

    # Replay source commands in room mesh order. RSP vertex cache and RDP
    # material state persist between display-list calls, as they do in gfx_pc.
    vertex_cache: list[dict | None] = [None] * 32
    state = {"geometry_mode": 0, "texture_enabled": False,
             "texture_scale": [0, 0], "texture_tile": 0,
             "texture_image_path": None, "texture_image_format": None,
             "texture_image_size_bits": None, "texture_image_width": None,
             "loaded_texture_by_tmem": {},
             "palette_path": None, "combiner_words": None,
             "other_mode_h": 0, "other_mode_l": 0,
             "tiles": {}, "tile_sizes": {}, "primitive_color": None,
             "environment_color": None, "fog_color": None}
    triangle_records = []
    texture_palettes: dict[str, set[str]] = {}
    execution_stack = []

    def execute_list(path: str, draw: dict, depth: int = 0) -> None:
        if depth > 32:
            unresolved_references.append({"kind": "display-list", "path": path,
                                          "reason": "nested display-list depth exceeds 32"})
            return
        if path in execution_stack:
            unresolved_references.append({"kind": "display-list", "path": path,
                                          "reason": "recursive display-list cycle"})
            return
        parsed = parsed_lists.get(path)
        if parsed is None:
            unresolved_references.append({"kind": "display-list", "path": path,
                                          "reason": "nested display list was not decoded"})
            return
        execution_stack.append(path)
        position = draw["position"]
        for instruction in parsed["instructions"]:
            opcode, w0, w1 = instruction["opcode"], instruction["w0"], instruction["w1"]
            if opcode == 0x32:
                target = instruction.get("resolved_path")
                array = decoded_resources.get(target)
                if array is None:
                    continue
                byte_offset = instruction["vertex_array_byte_offset"]
                count = instruction["vertex_count"]
                destination = instruction["vertex_destination"]
                if byte_offset % 16:
                    unresolved_references.append({"kind": "vertex-array", "source": path,
                                                  "target": target, "offset": byte_offset,
                                                  "reason": "vertex array offset is not 16-byte aligned"})
                    continue
                first = byte_offset // 16
                if destination < 0 or destination + count > len(vertex_cache) \
                        or first + count > array["count"]:
                    unresolved_references.append({"kind": "vertex-array", "source": path,
                                                  "target": target, "offset": byte_offset,
                                                  "count": count, "destination": destination,
                                                  "reason": "G_VTX_OTR_HASH range is outside array/cache"})
                    continue
                for index in range(count):
                    source_vertex = array["vertices"][first + index]
                    vertex_cache[destination + index] = {
                        "array_path": target, "array_index": first + index,
                        "position": [source_vertex["position"][axis] + position[axis]
                                     for axis in range(3)],
                        "world_position": [(source_vertex["position"][axis] + position[axis]) * scale
                                           for axis in range(3)],
                        "source_position": source_vertex["position"],
                        "uv_fixed_s10_5": source_vertex["uv_fixed_s10_5"],
                        "uv_texels": source_vertex["uv_texels"],
                        "cn_bytes": source_vertex["cn_bytes"],
                        "flag": source_vertex["flag"],
                    }
            elif opcode == 0x20:
                state["texture_image_path"] = instruction.get("resolved_path")
                state["texture_image_format"] = instruction["image_format"]
                state["texture_image_size_bits"] = instruction["image_size_bits"]
                state["texture_image_width"] = instruction["image_width"]
            elif opcode in (0xF3, 0xF4):
                # SETTIMG only selects the source for the next load. Render
                # tiles sample TMEM, which can keep an earlier texture while
                # SETTIMG is temporarily redirected to a TLUT.
                load_tile = (w1 >> 24) & 7
                tile_state = state["tiles"].get(load_tile, {})
                tmem_words = tile_state.get("tmem_words")
                if tmem_words is None:
                    unresolved_references.append({"kind": "texture-load", "source": path,
                                                  "offset": instruction["offset"],
                                                  "tile": load_tile,
                                                  "reason": "texture load tile has no prior SETTILE state"})
                elif state["texture_image_path"]:
                    state["loaded_texture_by_tmem"][tmem_words] = {
                        "path": state["texture_image_path"],
                        "format": state["texture_image_format"],
                        "size_bits": state["texture_image_size_bits"],
                        "width": state["texture_image_width"],
                    }
            elif opcode == 0xF0:
                if state["texture_image_path"]:
                    state["palette_path"] = state["texture_image_path"]
            elif opcode == 0xF5:
                state["tiles"][instruction["tile"]] = instruction["tile_state"]
            elif opcode == 0xF2:
                state["tile_sizes"][instruction["tile"]] = instruction["tile_rect_10_2"]
            elif opcode == 0xD7:
                texture = instruction["texture_state"]
                state["texture_enabled"] = texture["enabled"]
                state["texture_tile"] = texture["tile"]
                state["texture_scale"] = [texture["s_scale"], texture["t_scale"]]
            elif opcode == 0xD9:
                clear_mask = (~(w0 & 0x00FFFFFF)) & 0xFFFFFFFF
                state["geometry_mode"] = (state["geometry_mode"] & ~clear_mask) | w1
            elif opcode == 0xFC:
                state["combiner_words"] = [w0, w1]
            elif opcode in (0xE2, 0xE3):
                encoded_shift = (w0 >> 8) & 0xFF
                length = (w0 & 0xFF) + 1
                shift = (31 - encoded_shift - (length - 1)) if opcode == 0xE2 \
                        else (63 - encoded_shift - (length - 1) - 32)
                mode = state["other_mode_h"] if opcode == 0xE3 else state["other_mode_l"]
                mask = ((1 << length) - 1) << max(0, shift)
                mode = (mode & ~mask) | ((w1 << max(0, shift)) & mask)
                state["other_mode_h" if opcode == 0xE3 else "other_mode_l"] = mode & 0xFFFFFFFF
            elif opcode in (0xFA, 0xFB, 0xF8):
                rgba = [(w1 >> 24) & 0xFF, (w1 >> 16) & 0xFF,
                        (w1 >> 8) & 0xFF, w1 & 0xFF]
                key = {0xFA: "primitive_color", 0xFB: "environment_color",
                       0xF8: "fog_color"}[opcode]
                state[key] = rgba
            elif opcode == 0x31:
                target = instruction.get("resolved_path")
                if target:
                    execute_list(target, draw, depth + 1)
            elif opcode == 0xDE:
                # A raw segmented call cannot be resolved offline without the
                # original runtime segment table. Keep it visible in diagnostics.
                continue
            elif opcode in (0x05, 0x06):
                for triangle_index, indices in enumerate(instruction["triangles"]):
                    vertices = [vertex_cache[index] if 0 <= index < len(vertex_cache) else None
                                for index in indices]
                    if any(vertex is None for vertex in vertices):
                        unresolved_references.append({"kind": "triangle", "source": path,
                                                      "offset": instruction["offset"],
                                                      "indices": indices,
                                                      "reason": "triangle references an unloaded vertex"})
                        continue
                    render_tile = state["tiles"].get(state["texture_tile"], {})
                    tmem_words = render_tile.get("tmem_words")
                    loaded_texture = state["loaded_texture_by_tmem"].get(tmem_words)
                    texture_path = loaded_texture["path"] if loaded_texture else None
                    if state["texture_enabled"] and texture_path is None:
                        unresolved_references.append({"kind": "sampled-texture", "source": path,
                                                      "offset": instruction["offset"],
                                                      "tile": state["texture_tile"],
                                                      "tmem_words": tmem_words,
                                                      "reason": "sampled tile does not resolve to a texture loaded in TMEM"})
                    texture_format = loaded_texture["format"] if loaded_texture else state["texture_image_format"]
                    texture_size_bits = loaded_texture["size_bits"] if loaded_texture else state["texture_image_size_bits"]
                    texture_image_width = loaded_texture["width"] if loaded_texture else state["texture_image_width"]
                    palette_path = state["palette_path"]
                    if texture_path and texture_path in texture_payloads:
                        descriptor = texture_payloads[texture_path][0]
                        if descriptor["format_code"] in (3, 4) and palette_path:
                            texture_palettes.setdefault(texture_path, set()).add(palette_path)
                    material = {
                        "texture_image_path": texture_path,
                        "texture_image_format": texture_format,
                        "texture_image_size_bits": texture_size_bits,
                        "texture_image_width": texture_image_width,
                        "texture_image_tmem_words": tmem_words,
                        "current_settimg_path": state["texture_image_path"],
                        "palette_path": palette_path,
                        "texture_enabled": state["texture_enabled"],
                        "texture_tile": state["texture_tile"],
                        "texture_scale": state["texture_scale"],
                        "tile": state["tiles"].get(state["texture_tile"]),
                        "tile_rect_10_2": state["tile_sizes"].get(state["texture_tile"]),
                        "combiner_words": state["combiner_words"],
                        "geometry_mode": state["geometry_mode"],
                        "other_mode_h": state["other_mode_h"],
                        "other_mode_l": state["other_mode_l"],
                        "primitive_color": state["primitive_color"],
                        "environment_color": state["environment_color"],
                        "fog_color": state["fog_color"],
                    }
                    material_key = hashlib.sha256(json.dumps(material, sort_keys=True,
                                                             separators=(",", ":")).encode()).hexdigest()[:16]
                    triangle_records.append({
                        "display_list": path, "mesh_polygon": draw["polygon_index"],
                        "render_pass": draw["render_pass"],
                        "instruction_offset": instruction["offset"],
                        "triangle_in_instruction": triangle_index,
                        "material_id": material_key, "material": material,
                        "vertices": vertices,
                    })
        execution_stack.pop()

    for draw in pending_lists:
        execute_list(draw["path"], draw)

    texture_records = []
    texture_decode_errors = []
    texture_rgba = {}
    for path, (descriptor, payload, version, endian) in sorted(texture_payloads.items()):
        palette_paths = sorted(texture_palettes.get(path, set()))
        palette = None
        if descriptor["format_code"] in (3, 4):
            if len(palette_paths) != 1:
                texture_decode_errors.append({"path": path,
                                              "reason": "indexed texture does not resolve to exactly one TLUT",
                                              "palette_paths": palette_paths})
                continue
            palette_resource = texture_payloads.get(palette_paths[0])
            if palette_resource is None:
                texture_decode_errors.append({"path": path,
                                              "reason": "TLUT is not a supported texture resource",
                                              "palette_path": palette_paths[0]})
                continue
            palette_desc, palette_bytes, _, _ = palette_resource
            if palette_desc["format_code"] != 2:
                texture_decode_errors.append({"path": path, "palette_path": palette_paths[0],
                                              "reason": "TLUT payload is not RGBA16"})
                continue
            palette_pixels = decode_texture_rgba(palette_desc, palette_bytes)
            palette = [list(palette_pixels[i:i + 4]) for i in range(0, len(palette_pixels), 4)]
        try:
            rgba = decode_texture_rgba(descriptor, payload, palette)
            texture_rgba[path] = rgba
            texture_records.append({"path": path, "format": descriptor["format"],
                                    "width": descriptor["width"], "height": descriptor["height"],
                                    "payload_bytes": len(payload), "palette_paths": palette_paths,
                                    "decoded_rgba8_bytes": len(rgba)})
        except ImportFailure as exc:
            texture_decode_errors.append({"path": path, "reason": str(exc)})

    return {
        "status": "decoded-host-side",
        "room_mesh_count": len(room_meshes),
        "draw_call_count": len(pending_lists),
        "display_lists": sorted(set(display_list_paths)),
        "display_list_count": len(set(display_list_paths)),
        "instruction_count": sum(item["instruction_count"] for item in parsed_lists.values()),
        "source_triangle_count": sum(item["triangle_count"] for item in parsed_lists.values()),
        "decoded_triangle_count": len(triangle_records),
        "vertex_arrays": sorted(referenced_vertex_arrays),
        "textures": sorted(referenced_textures),
        "decoded_texture_count": len(texture_records),
        "texture_records": texture_records,
        "texture_rgba8": texture_rgba,
        "texture_decode_errors": texture_decode_errors,
        "texture_palettes": {path: sorted(paths) for path, paths in sorted(texture_palettes.items())},
        "triangles": triangle_records,
        "unresolved_references": unresolved_references,
        "unsupported_segmented_display_list_calls": unsupported_segmented_calls,
        "display_list_marker_mismatches": marker_mismatches,
        "runtime_integration": "lowered to the SM64 Gfx/Collision level ABI; opt-in level registration uses the existing Mario/TERRAIN/lvl_init_or_update path",
    }


def _native_symbol(kind: str, path: str, suffix: str = "") -> str:
    return f"hyrule_{kind}_{crc64_resource_path(path):016x}{suffix}"


def _c_float(value: float) -> str:
    text = f"{value:.8g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def _gfx_initializer(w0: int, w1: str | int = 0) -> str:
    rhs = w1 if isinstance(w1, str) else f"0x{w1:08x}u"
    return f"    {{ .words = {{ 0x{w0:08x}u, (uintptr_t)({rhs}) }} }},"


def emit_native_level_include(output_path: Path, room_meshes: list[dict],
                              decoded_resources: dict, texture_payloads: dict,
                              display_list_bodies: dict, archive_paths: list[str],
                              endian_by_path: dict, collision: dict | None,
                              scale: float, spawn: dict | None) -> dict:
    """Lower supported O2R resources to the game's existing Gfx/Collision ABI.

    OTR-specific hashed SETTIMG and VTX commands are rewritten to the standard
    F3DEX2 commands consumed by gfx_pc.c. Render tiles continue through the
    normal N64 texture/sampler/combine path. Direct segmented G_DL calls have
    no O2R resource identity and are replaced by SPNOOP with an explicit count.
    """
    hash_paths: dict[int, list[str]] = {}
    for path in archive_paths:
        hash_paths.setdefault(crc64_resource_path(path), []).append(path)

    draw_calls = []
    by_polygon_lists: dict[int, dict[str, list[str]]] = {}
    parsed_lists: dict[str, dict] = {}
    referenced_arrays: set[str] = set()
    referenced_textures: set[str] = set()
    list_to_polygons: dict[str, set[int]] = {}
    for mesh_record in room_meshes:
        mesh = mesh_record["mesh"]
        for polygon_index, polygon in enumerate(mesh.get("polygons", [])):
            by_polygon_lists[polygon_index] = {"opaque": [], "translucent": []}
            for render_pass, key in (("opaque", "opaque_display_list"),
                                     ("translucent", "translucent_display_list")):
                path = polygon.get(key)
                if path:
                    draw_calls.append((path, polygon_index, render_pass))
                    by_polygon_lists[polygon_index][render_pass].append(path)
                    list_to_polygons.setdefault(path, set()).add(polygon_index)

    queue = [path for path, _, _ in draw_calls]
    while queue:
        path = queue.pop()
        if path in parsed_lists:
            continue
        raw = display_list_bodies.get(path)
        if raw is None:
            raise ImportFailure(f"native Gfx lowering: missing display-list resource {path}")
        parsed = parse_display_list(raw["body"], endian_by_path[path], path)
        parsed_lists[path] = parsed
        for instruction in parsed["instructions"]:
            opcode = instruction["opcode"]
            if opcode not in (0x20, 0x31, 0x32):
                continue
            key = int(instruction["resource_hash"], 16)
            candidates = hash_paths.get(key, [])
            if len(candidates) != 1:
                raise ImportFailure(f"native Gfx lowering: unresolved {instruction['name']} reference "
                                    f"{instruction['resource_hash']} in {path}")
            target = candidates[0]
            instruction["resolved_path"] = target
            if opcode == 0x20:
                if target not in texture_payloads:
                    raise ImportFailure(f"native Gfx lowering: texture data is unavailable for {target}")
                referenced_textures.add(target)
            elif opcode == 0x31:
                if target not in display_list_bodies:
                    raise ImportFailure(f"native Gfx lowering: nested display list {target} is unavailable")
                if path not in list_to_polygons:
                    list_to_polygons[path] = set()
                list_to_polygons[path].update(list_to_polygons.get(path, set()))
                queue.append(target)
            elif opcode == 0x32:
                if target not in decoded_resources:
                    raise ImportFailure(f"native Gfx lowering: vertex array {target} is unavailable")
                referenced_arrays.add(target)

    # A nested list inherits the polygon transform of each caller. Use the
    # caller's variants when emitting references; the field's default root
    # lists currently contain no OTR-hashed DL calls.
    variants = {(path, polygon) for path, polygons in list_to_polygons.items()
                for polygon in polygons}
    changed = True
    while changed:
        changed = False
        for source_path, polygon in tuple(variants):
            for instruction in parsed_lists[source_path]["instructions"]:
                if instruction["opcode"] != 0x31:
                    continue
                target = instruction.get("resolved_path")
                if target and (target, polygon) not in variants:
                    variants.add((target, polygon))
                    list_to_polygons.setdefault(target, set()).add(polygon)
                    changed = True

    symbols = {}
    for path, polygon in variants:
        symbols[(path, polygon)] = _native_symbol("dl", path, f"_p{polygon}")
    vertex_symbols = {}
    texture_symbols = {}
    lines = ["/* Generated from the user-provided O2R archive. Do not commit this file. */",
             "#include <ultra64.h>", "#include <stdint.h>",
             "#include \"types.h\"", "#include \"surface_terrains.h\"", ""]

    for path in sorted(referenced_textures):
        descriptor, payload, _, _ = texture_payloads[path]
        symbol = _native_symbol("tex", path)
        texture_symbols[path] = symbol
        lines.append(f"static const u8 {symbol}[{len(payload)}] = {{")
        for offset in range(0, len(payload), 16):
            lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in payload[offset:offset + 16]) + ",")
        lines.extend(["};", ""])

    for path, polygon in sorted(variants, key=lambda item: (item[1], item[0])):
        if path not in parsed_lists:
            continue
        mesh = next((record["mesh"] for record in room_meshes
                     if polygon < len(record["mesh"].get("polygons", []))), None)
        position = (mesh["polygons"][polygon].get("position", [0, 0, 0])
                    if mesh else [0, 0, 0])
        for instruction in parsed_lists[path]["instructions"]:
            if instruction["opcode"] != 0x32:
                continue
            array_path = instruction["resolved_path"]
            key = (array_path, polygon)
            if key in vertex_symbols:
                continue
            array = decoded_resources[array_path]
            symbol = _native_symbol("vtx", array_path, f"_p{polygon}")
            vertex_symbols[key] = symbol
            lines.append(f"static const Vtx {symbol}[{array['count']}] = {{")
            for vertex in array["vertices"]:
                xyz = [_c_float((vertex["position"][axis] + position[axis]) * scale)
                       for axis in range(3)]
                uv = vertex["uv_fixed_s10_5"]
                color = ", ".join(f"0x{value:02x}" for value in vertex["cn_bytes"])
                lines.append(f"    {{{{{{{xyz[0]}, {xyz[1]}, {xyz[2]}}}, "
                             f"{vertex['flag']}, {{{uv[0]}, {uv[1]}}}, {{{color}}}}}}},")
            lines.extend(["};", ""])

    for _, symbol in sorted(symbols.items(), key=lambda item: item[1]):
        lines.append(f"static const Gfx {symbol}[];")
    lines.append("")

    omitted_segmented_calls = 0
    for path, polygon in sorted(variants, key=lambda item: (item[1], item[0])):
        if path not in parsed_lists:
            continue
        symbol = symbols[(path, polygon)]
        lines.append(f"static const Gfx {symbol}[] = {{")
        for instruction in parsed_lists[path]["instructions"]:
            opcode = instruction["opcode"]
            w0, w1 = instruction["w0"], instruction["w1"]
            if opcode == 0x33:
                continue
            if opcode == 0x32:
                target = instruction["resolved_path"]
                byte_offset = instruction["vertex_array_byte_offset"]
                if byte_offset % 16:
                    raise ImportFailure(f"native Gfx lowering: unaligned vertex offset in {path}")
                count = instruction["vertex_count"]
                destination = instruction["vertex_destination"]
                array = decoded_resources[target]
                first = byte_offset // 16
                if first + count > array["count"] or destination < 0 or destination + count > 32:
                    raise ImportFailure(f"native Gfx lowering: invalid vertex range in {path}")
                vertex_symbol = vertex_symbols[(target, polygon)]
                lines.append(_gfx_initializer((w0 & 0x00FFFFFF) | 0x01000000,
                                              f"{vertex_symbol} + {first}"))
            elif opcode == 0x20:
                target = instruction["resolved_path"]
                lines.append(_gfx_initializer((w0 & 0x00FFFFFF) | 0xFD000000,
                                              texture_symbols[target]))
            elif opcode == 0x31:
                target = instruction["resolved_path"]
                target_symbol = symbols.get((target, polygon))
                if not target_symbol:
                    raise ImportFailure(f"native Gfx lowering: nested list {target} lacks polygon variant")
                lines.append(_gfx_initializer((w0 & 0x00FFFFFF) | 0xDE000000, target_symbol))
            elif opcode == 0xDE:
                # O2R keeps three original segmented calls in this room. Their
                # segment base table is not present in the selected resources.
                omitted_segmented_calls += 1
                lines.append(_gfx_initializer(0xE0000000))
            elif opcode in (0x35, 0x36, 0x42):
                raise ImportFailure(f"native Gfx lowering: {instruction['name']} is not supported in {path}")
            else:
                lines.append(_gfx_initializer(w0, w1))
        lines.append(_gfx_initializer(0xDF000000))
        lines.extend(["};", ""])

    for render_pass, public_name in (("opaque", "hyrule_field_opaque_dl"),
                                     ("translucent", "hyrule_field_translucent_dl")):
        lines.append(f"const Gfx {public_name}[] = {{")
        for path, polygon, draw_pass in draw_calls:
            if draw_pass != render_pass:
                continue
            lines.append(_gfx_initializer(0xDE000000, symbols[(path, polygon)]))
        lines.append(_gfx_initializer(0xDF000000))
        lines.extend(["};", ""])

    collision_triangles = 0
    collision_vertices = 0
    if collision is not None:
        collision_vertices = len(collision["vertices"])
        collision_triangles = len(collision["polygons"])
        lines.extend(["const Collision hyrule_field_collision[] = {", "    COL_INIT(),",
                      f"    COL_VERTEX_INIT({collision_vertices}),"])
        for vertex in collision["vertices"]:
            scaled = [int(round(value * scale)) for value in vertex]
            if any(value < -32768 or value > 32767 for value in scaled):
                raise ImportFailure("native collision scale produces coordinates outside SM64 s16 range")
            lines.append(f"    COL_VERTEX({scaled[0]}, {scaled[1]}, {scaled[2]}),")
        lines.append(f"    COL_TRI_INIT(SURFACE_DEFAULT, {collision_triangles}),")
        for polygon in collision["polygons"]:
            a, b, c = polygon["vertex_indices"]
            lines.append(f"    COL_TRI({a}, {b}, {c}),")
        lines.extend(["    COL_TRI_STOP(),", "    COL_END(),", "};", ""])

    spawn_position = spawn.get("position") if spawn else None
    spawn_yaw = int(spawn.get("yaw", 0)) if spawn else 0
    if spawn_position is None:
        spawn_position = [0, 0, 0]
    scaled_spawn = [int(round(value * scale)) for value in spawn_position]
    if any(value < -32768 or value > 32767 for value in scaled_spawn):
        raise ImportFailure("spawn scale produces coordinates outside SM64 MARIO_POS s16 range")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    spawn_header_path = output_path.with_name("hyrule_field_spawn.h")
    spawn_header_path.write_text(
        "#ifndef HYRULE_FIELD_SPAWN_GENERATED_H\n"
        "#define HYRULE_FIELD_SPAWN_GENERATED_H\n"
        f"#define HYRULE_FIELD_SPAWN_X {scaled_spawn[0]}\n"
        f"#define HYRULE_FIELD_SPAWN_Y {scaled_spawn[1]}\n"
        f"#define HYRULE_FIELD_SPAWN_Z {scaled_spawn[2]}\n"
        f"#define HYRULE_FIELD_SPAWN_YAW ((s16){spawn_yaw})\n"
        "#endif\n", encoding="utf-8")
    return {"include": str(output_path), "display_list_count": len(variants),
            "opaque_calls": sum(len(v["opaque"]) for v in by_polygon_lists.values()),
            "translucent_calls": sum(len(v["translucent"]) for v in by_polygon_lists.values()),
            "vertex_array_count": len(vertex_symbols), "texture_count": len(texture_symbols),
            "collision_vertices": collision_vertices, "collision_triangles": collision_triangles,
            "omitted_segmented_display_list_calls": omitted_segmented_calls,
            "spawn_header": str(spawn_header_path),
            "uses_existing_gfx_and_collision_abi": True}


def read_actor_entries(reader: Reader, label: str) -> list[dict]:
    count = reader.count(label, 100_000)
    result = []
    for _ in range(count):
        result.append({"id": reader.u16(), "position": [reader.i16(), reader.i16(), reader.i16()],
                       "rotation": [reader.i16(), reader.i16(), reader.i16()], "params": reader.u16()})
    return result


def parse_mesh(reader: Reader) -> dict:
    data = reader.i8()
    mesh_type = reader.i8()
    poly_count = 1 if mesh_type == 1 else reader.i8()
    if mesh_type not in (0, 1, 2) or poly_count < 0 or poly_count > 127:
        raise ImportFailure(f"{reader.label}: unsupported mesh type/count {mesh_type}/{poly_count}")
    polygons = []
    images = []
    if mesh_type == 1:
        image_format = reader.u8()
        reader.string(); reader.string()  # exporter duplicates the two dlist path slots here
        image_count = reader.count("background image", 4096)
        for _ in range(image_count):
            image = {"unknown": reader.u16(), "id": reader.u8(), "path": reader.string(),
                     "unknown_0c": reader.u32(), "tlut": reader.u32(),
                     "width": reader.u16(), "height": reader.u16(), "format": reader.u8(),
                     "size": reader.u8(), "mode": reader.u16(), "tlut_count": reader.u16()}
            images.append(image)
        poly_type = reader.i8()
        opaque, translucent = reader.string(), reader.string()
        polygons.append({"poly_type": poly_type, "opaque_display_list": opaque or None,
                         "translucent_display_list": translucent or None})
        return {"data": data, "type": mesh_type, "background_format": image_format,
                "background_images": images, "polygons": polygons}
    for _ in range(poly_count):
        poly_type = reader.i8()
        polygon = {"poly_type": poly_type}
        if mesh_type == 2:
            polygon["position"] = [reader.i16(), reader.i16(), reader.i16()]
            polygon["unknown"] = reader.i16()
        opaque, translucent = reader.string(), reader.string()
        polygon["opaque_display_list"] = opaque or None
        polygon["translucent_display_list"] = translucent or None
        polygons.append(polygon)
    return {"data": data, "type": mesh_type, "polygons": polygons, "background_images": images}


def parse_scene(body: bytes, endian: str, path: str) -> dict:
    reader = Reader(body, endian, path)
    count = reader.count("scene command", 4096)
    commands = []
    dependencies = set()
    completed = True
    unsupported = None
    for index in range(count):
        command_offset = reader.offset
        command_id = reader.i32()
        name = COMMAND_NAMES.get(command_id, f"Unknown_{command_id}")
        try:
            if command_id in (0, 1):
                payload = {"entries": read_actor_entries(reader, name)}
            elif command_id == 2:
                unsupported = {"index": index, "id": command_id, "name": name,
                               "offset": command_offset, "reason": "cutscene camera payload is not decoded"}
                completed = False
                break
            elif command_id == 3:
                collision_path = reader.string()
                payload = {"collision_header": collision_path}
                dependencies.add(collision_path)
            elif command_id == 4:
                room_count = reader.count("room", 4096)
                rooms = []
                for _ in range(room_count):
                    room_path = reader.string()
                    rooms.append({"path": room_path, "vrom_start": reader.i32(), "vrom_end": reader.i32()})
                    dependencies.add(room_path)
                payload = {"rooms": rooms}
            elif command_id == 5:
                payload = {"wind": [reader.i8(), reader.i8(), reader.i8(), reader.u8()]}
            elif command_id == 6:
                entry_count = reader.count("entrance", 4096)
                payload = {"entrances": [{"spawn": reader.i8(), "room": reader.i8()}
                                           for _ in range(entry_count)]}
            elif command_id == 7:
                payload = {"elf_message": reader.i8(), "global_object": reader.i16()}
            elif command_id == 8:
                payload = {"gameplay_flags": reader.i8(), "gameplay_flags2": reader.i32()}
            elif command_id == 9:
                unsupported = {"index": index, "id": command_id, "name": name,
                               "offset": command_offset, "reason": "reserved scene command has no registered factory"}
                completed = False
                break
            elif command_id == 10:
                payload = {"mesh": parse_mesh(reader)}
                for polygon in payload["mesh"]["polygons"]:
                    for key in ("opaque_display_list", "translucent_display_list"):
                        if polygon.get(key):
                            dependencies.add(polygon[key])
                for image in payload["mesh"].get("background_images", []):
                    dependencies.add(image["path"])
            elif command_id == 11:
                object_count = reader.count("object", 65536)
                payload = {"objects": [reader.u16() for _ in range(object_count)]}
            elif command_id == 12:
                light_count = reader.count("light", 65536)
                lights = []
                for _ in range(light_count):
                    lights.append({"type": reader.u8(), "position": [reader.i16(), reader.i16(), reader.i16()],
                                   "color": [reader.u8(), reader.u8(), reader.u8()],
                                   "draw_glow": reader.u8(), "radius": reader.i16()})
                payload = {"lights": lights}
            elif command_id == 13:
                path_count = reader.count("path", 65536)
                paths = [reader.string() for _ in range(path_count)]
                payload = {"paths": paths}; dependencies.update(paths)
            elif command_id == 14:
                transition_count = reader.count("transition actor", 65536)
                transitions = []
                for _ in range(transition_count):
                    front_room, front_effect, back_room, back_effect = [reader.u8() for _ in range(4)]
                    transitions.append({"front_room": front_room, "front_effect": front_effect,
                                        "back_room": back_room, "back_effect": back_effect,
                                        "id": reader.i16(), "position": [reader.i16(), reader.i16(), reader.i16()],
                                        "rotation_y": reader.i16(), "params": reader.u16()})
                payload = {"transition_actors": transitions}
            elif command_id == 15:
                light_count = reader.count("lighting setting", 4096)
                settings = []
                for _ in range(light_count):
                    settings.append({"ambient": list(reader.take(3)), "light1_direction": list(reader.take(3)),
                                     "light1_color": list(reader.take(3)), "light2_direction": list(reader.take(3)),
                                     "light2_color": list(reader.take(3)), "fog_color": list(reader.take(3)),
                                     "fog_near": reader.i16(), "fog_far": reader.u16()})
                payload = {"lighting_settings": settings}
            elif command_id == 16:
                payload = {"time": [reader.i8(), reader.i8(), reader.i8()]}
            elif command_id == 17:
                payload = {"skybox": [reader.i8(), reader.i8(), reader.i8(), reader.i8()]}
            elif command_id == 18:
                payload = {"skybox_modifier": [reader.i8(), reader.i8()]}
            elif command_id == 19:
                exit_count = reader.count("exit", 4096)
                payload = {"exits": [reader.u16() for _ in range(exit_count)]}
            elif command_id == 20:
                payload = {}
            elif command_id == 21:
                payload = {"sound": [reader.i8(), reader.i8(), reader.i8()]}
            elif command_id == 22:
                payload = {"echo": reader.i8()}
            elif command_id == 23:
                cutscene_path = reader.string()
                payload = {"cutscene": cutscene_path}
                if cutscene_path:
                    dependencies.add(cutscene_path)
            elif command_id == 24:
                header_count = reader.count("alternate header", 4096)
                headers = [reader.string() for _ in range(header_count)]
                payload = {"alternate_headers": headers}; dependencies.update(h for h in headers if h)
            elif command_id == 25:
                payload = {"camera_settings": [reader.i8(), reader.i32()]}
            else:
                unsupported = {"index": index, "id": command_id, "name": name,
                               "offset": command_offset, "reason": "command payload is not decoded"}
                completed = False
                break
        except ImportFailure as exc:
            raise ImportFailure(f"{path}: command {index} {name}: {exc}") from exc
        commands.append({"index": index, "id": command_id, "name": name,
                         "byte_offset": command_offset, **payload})
        if command_id == 20:
            if index + 1 != count:
                unsupported = {"index": index + 1, "reason": "commands follow EndMarker"}
                completed = False
            break
    return {"command_count": count, "commands": commands,
            "dependencies": sorted(dependencies), "fully_decoded": completed,
            "unsupported_at": unsupported, "consumed_bytes": reader.offset}


def coordinate(point: list[int], scale: float) -> list[float]:
    # The runtime and OoT use X/Y/Z with Y vertical. Keep axes explicit and
    # preserve the source coordinates until scene calibration is device-tested.
    return [point[0] * scale, point[1] * scale, point[2] * scale]


def collision_floor_at(collision: dict, x: float, y: float, z: float,
                      max_above: float = 50.0, max_below: float = 250.0) -> dict | None:
    """Return the highest upward-facing collision triangle below a spawn point."""
    vertices = collision["vertices"]
    best = None
    for index, polygon in enumerate(collision["polygons"]):
        if polygon["normal"][1] <= 0:
            continue
        a, b, c = (vertices[i] for i in polygon["vertex_indices"])
        denominator = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
        if abs(denominator) < 1e-9:
            continue
        wa = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / denominator
        wb = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / denominator
        wc = 1.0 - wa - wb
        if min(wa, wb, wc) < -1e-6:
            continue
        floor_y = wa * a[1] + wb * b[1] + wc * c[1]
        if floor_y > y + max_above or floor_y < y - max_below:
            continue
        if best is None or floor_y > best["floor_y"]:
            best = {"triangle": index, "floor_y": floor_y,
                    "spawn_y_delta": y - floor_y,
                    "surface_type": polygon["surface_type"],
                    "normal": polygon["normal"]}
    return best


def collision_geometry_diagnostics(collision: dict) -> dict:
    vertices = collision["vertices"]
    degenerate = 0
    aligned = 0
    reversed_winding = 0
    other_normal_mismatch = 0
    for polygon in collision["polygons"]:
        a, b, c = (vertices[i] for i in polygon["vertex_indices"])
        ab = [b[i] - a[i] for i in range(3)]
        ac = [c[i] - a[i] for i in range(3)]
        cross = [ab[1] * ac[2] - ab[2] * ac[1],
                 ab[2] * ac[0] - ab[0] * ac[2],
                 ab[0] * ac[1] - ab[1] * ac[0]]
        cross_length = math.sqrt(sum(value * value for value in cross))
        normal = polygon["normal"]
        normal_length = math.sqrt(sum(value * value for value in normal))
        if cross_length == 0.0 or normal_length == 0.0:
            degenerate += 1
            continue
        alignment = sum(cross[i] * normal[i] for i in range(3)) / (cross_length * normal_length)
        if alignment >= 0.99:
            aligned += 1
        elif alignment <= -0.99:
            reversed_winding += 1
        else:
            other_normal_mismatch += 1
    expected_min = collision["bounds"]["min"]
    expected_max = collision["bounds"]["max"]
    if vertices:
        actual_min = [min(vertex[axis] for vertex in vertices) for axis in range(3)]
        actual_max = [max(vertex[axis] for vertex in vertices) for axis in range(3)]
        bounds_contain_vertices = all(expected_min[i] <= actual_min[i] <= actual_max[i] <= expected_max[i]
                                      for i in range(3))
    else:
        bounds_contain_vertices = False
    return {"triangle_count": len(collision["polygons"]), "degenerate_triangles": degenerate,
            "source_normal_aligned_triangles": aligned, "reversed_winding_triangles": reversed_winding,
            "other_normal_mismatches": other_normal_mismatch,
            "declared_bounds_contain_vertices": bounds_contain_vertices}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_collision_outputs(output_dir: Path, collision: dict, scale: float, stem: str) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    obj_path = output_dir / f"{stem}.collision.obj"
    with obj_path.open("w", encoding="utf-8", newline="\n") as obj:
        obj.write("# OoT collision mesh; source triangle order retained\n")
        for point in collision["vertices"]:
            x, y, z = coordinate(point, scale)
            obj.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        for poly in collision["polygons"]:
            a, b, c = (index + 1 for index in poly["vertex_indices"])
            obj.write(f"f {a} {b} {c}\n")
    json_path = output_dir / f"{stem}.collision.json"
    collision = {**collision, "geometry_validation": collision_geometry_diagnostics(collision)}
    json_path.write_text(json.dumps(collision, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return {"obj": obj_path.name, "metadata": json_path.name,
            "vertices": len(collision["vertices"]), "triangles": len(collision["polygons"]),
            "scale": scale, "geometry_validation": collision["geometry_validation"]}


def import_archive(archive_path: Path, output_dir: Path, scene_prefix: str, scale: float) -> dict:
    if not archive_path.is_file():
        raise ImportFailure(f"O2R archive not found: {archive_path}. Supply oot.o2r as the input path.")
    if not math.isfinite(scale) or scale <= 0.0:
        raise ImportFailure("--scale must be a positive finite number")
    scene_prefix = safe_archive_path(scene_prefix).rstrip("/")
    try:
        archive = zipfile.ZipFile(archive_path, "r")
    except (OSError, zipfile.BadZipFile) as exc:
        raise ImportFailure(f"not a readable O2R ZIP archive: {exc}") from exc

    with archive:
        infos = archive.infolist()
        bad = archive.testzip()
        if bad:
            raise ImportFailure(f"ZIP CRC validation failed at entry {bad}")
        files = []
        selected = []
        index_by_name = {}
        for info in infos:
            if info.is_dir():
                continue
            path = safe_archive_path(info.filename)
            if path in index_by_name:
                raise ImportFailure(f"duplicate ZIP resource path: {path}")
            index_by_name[path] = info
            files.append({"path": path, "size": info.file_size, "crc32": f"{info.CRC:08x}"})
            if path == scene_prefix or path.startswith(scene_prefix + "/"):
                selected.append(path)
        if not selected:
            raise ImportFailure(f"no resources under scene path {scene_prefix!r}")

        files.sort(key=lambda item: item["path"])
        resources = []
        decoded = {}
        scene_paths = []
        collision_paths = []
        vertex_arrays = []
        textures = []
        texture_payloads = {}
        display_list_bodies = {}
        endian_by_path = {}
        for path in sorted(selected):
            data = archive.read(index_by_name[path])
            header = read_resource_header(data, path)
            record = {"path": path, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(), **header}
            if header["status"] == "ok":
                body = data[OTR_HEADER_SIZE:]
                endian = "<" if header["endian"] == "little" else ">"
                endian_by_path[path] = endian
                if header["resource_type"] == "Scene":
                    scene_paths.append(path)
                    if header["version"] != 0:
                        record["unsupported_reason"] = f"scene resource version {header['version']} is not supported (need 0)"
                    else:
                        try:
                            parsed = parse_scene(body, endian, path)
                            decoded[path] = parsed
                            record["decoded"] = ("scene-metadata-v1" if parsed["fully_decoded"]
                                                 else "scene-metadata-partial-v1")
                            record["decode_status"] = "complete" if parsed["fully_decoded"] else "partial"
                            record["dependencies"] = parsed["dependencies"]
                            if parsed["unsupported_at"]:
                                record["unsupported_at"] = parsed["unsupported_at"]
                        except ImportFailure as exc:
                            record["decode_error"] = str(exc)
                elif header["resource_type"] == "CollisionHeader":
                    collision_paths.append(path)
                    if header["version"] != 0:
                        record["unsupported_reason"] = f"collision resource version {header['version']} is not supported (need 0)"
                    else:
                        try:
                            decoded[path] = parse_collision(body, endian, path)
                            record["decoded"] = "collision-header-v0"
                            record["decode_status"] = "complete"
                        except ImportFailure as exc:
                            record["decode_error"] = str(exc)
                elif header["resource_type"] == "Array":
                    if header["version"] != 0:
                        record["unsupported_reason"] = f"array resource version {header['version']} is not supported (need 0)"
                    else:
                        try:
                            parsed = parse_vertex_array(body, endian, path)
                            decoded[path] = parsed
                            vertex_arrays.append({"path": path, **parsed})
                            record["decoded"] = "vertex-array-v0"
                            record["decode_status"] = "complete"
                            record["array_type"] = parsed["array_type_name"]
                            record["element_count"] = parsed["count"]
                        except ImportFailure as exc:
                            record["decode_error"] = str(exc)
                elif header["resource_type"] == "DisplayList":
                    if header["version"] != 0:
                        record["unsupported_reason"] = f"display-list resource version {header['version']} is not supported (need 0)"
                    else:
                        try:
                            parsed = parse_display_list(body, endian, path)
                            decoded[path] = parsed
                            display_list_bodies[path] = {"body": body, "version": header["version"]}
                            record["decoded"] = "f3dex2-display-list-v0" if parsed["ucode"] == 4 else "display-list-v0"
                            record["decode_status"] = "complete"
                            record["ucode"] = parsed["ucode"]
                            record["instruction_count"] = parsed["instruction_count"]
                            record["triangle_count"] = parsed["triangle_count"]
                        except ImportFailure as exc:
                            record["decode_error"] = str(exc)
                elif header["resource_type"] == "Texture":
                    try:
                        descriptor = parse_texture_descriptor(body, endian, header["version"], path)
                        textures.append({"path": path, **descriptor})
                        texture_payloads[path] = (descriptor, body[descriptor["payload_offset"]:],
                                                  header["version"], endian)
                        record["decoded"] = f"texture-header-v{header['version']}"
                        record["decode_status"] = "metadata-only"
                        record["texture"] = descriptor
                    except ImportFailure as exc:
                        record["decode_error"] = str(exc)
            resources.append(record)

        preferred_scene_path = str(PurePosixPath(scene_prefix) / PurePosixPath(scene_prefix).name)
        scene_path = preferred_scene_path if preferred_scene_path in scene_paths else None
        if scene_path is None:
            conventional_scene_path = str(PurePosixPath(scene_prefix) / "Scene")
            if conventional_scene_path in scene_paths:
                scene_path = conventional_scene_path
            elif len(scene_paths) == 1:
                scene_path = scene_paths[0]
        if scene_path is None:
            if scene_paths:
                raise ImportFailure("multiple scene resources found but no canonical root was identified; "
                                    f"expected {preferred_scene_path!r}, found {len(scene_paths)} candidates")
            raise ImportFailure(f"no OROM scene resource found under {scene_prefix!r}; indexed {len(selected)} files")
        if scene_path not in decoded:
            problem = next((item.get("unsupported_reason") or item.get("decode_error")
                            for item in resources if item["path"] == scene_path), "unknown scene parse failure")
            raise ImportFailure(f"scene resource could not be decoded: {scene_path}: {problem}")

        output_dir.mkdir(parents=True, exist_ok=True)
        collision_outputs = []
        scene = decoded.get(scene_path)
        root_dependency_paths = set(scene["dependencies"] if scene else [])
        scene_dependency_paths = set(root_dependency_paths)
        scene_nodes = []
        for candidate in sorted(scene_paths):
            parsed_scene = decoded.get(candidate)
            if parsed_scene is None:
                continue
            scene_dependency_paths.update(parsed_scene["dependencies"])
            if candidate == scene_path:
                role = "root"
            elif "_room_" in PurePosixPath(candidate).name:
                role = "room"
            elif "Set_" in PurePosixPath(candidate).name:
                role = "alternate-header"
            else:
                role = "scene-resource"
            scene_nodes.append({"path": candidate, "role": role, **parsed_scene})

        room_meshes = []
        if scene is not None:
            room_paths = []
            for command in scene["commands"]:
                if command["id"] == 4:
                    room_paths.extend(room["path"] for room in command["rooms"])
            for room_path in room_paths:
                room_scene = decoded.get(room_path)
                if room_scene is None:
                    continue
                for command in room_scene["commands"]:
                    if command["id"] == 10:
                        room_meshes.append({"room": room_path, "mesh": command["mesh"]})
        render_data = decode_room_render_data(room_meshes, decoded, decoded,
                                              texture_payloads, display_list_bodies,
                                              [entry["path"] for entry in files], endian_by_path, scale)
        render_data_json = {key: value for key, value in render_data.items() if key != "texture_rgba8"}
        texture_output_dir = output_dir / "render" / "textures"
        texture_output_dir.mkdir(parents=True, exist_ok=True)
        for texture_path, pixels in render_data["texture_rgba8"].items():
            texture_name = f"{crc64_resource_path(texture_path):016x}.rgba8"
            (texture_output_dir / texture_name).write_bytes(pixels)
            next(item for item in render_data_json["texture_records"] if item["path"] == texture_path)["rgba8_file"] = f"textures/{texture_name}"
        render_data_json["texture_rgba8_format"] = "row-major RGBA8, tightly packed, no row padding"
        render_manifest_path = output_dir / "render" / "hyrule-render.json"
        render_manifest_path.write_text(json.dumps(render_data_json, sort_keys=True, indent=2) + "\n",
                                        encoding="utf-8")
        for path in collision_paths:
            collision = decoded.get(path)
            if not collision:
                continue
            if path not in root_dependency_paths:
                continue
            stem = "spot00" if len(collision_outputs) == 0 else f"spot00_{len(collision_outputs)}"
            collision_outputs.append({**write_collision_outputs(output_dir, collision, scale, stem),
                                      "source_resource": path})

        # The manifest preserves all resource paths and dependencies. Payloads
        # are emitted only for parsers that have a tested schema.
        dependency_records = []
        for dependency in sorted(scene_dependency_paths):
            dependency = safe_archive_path(dependency)
            info = index_by_name.get(dependency)
            if info is None:
                dependency_records.append({"path": dependency, "status": "missing"})
                continue
            try:
                dependency_data = archive.read(info)
            except (OSError, RuntimeError, zipfile.BadZipFile) as exc:
                dependency_records.append({"path": dependency, "status": "unreadable", "error": str(exc)})
                continue
            dependency_header = read_resource_header(dependency_data, dependency)
            dependency_records.append({
                "path": dependency, "status": "available", "size": info.file_size,
                "resource_type": dependency_header.get("resource_type"),
                "resource_type_code": dependency_header.get("resource_type_code"),
                "version": dependency_header.get("version"),
            })
        collision_bounds = []
        primary_collision = None
        for output_record in collision_outputs:
            parsed = decoded.get(output_record["source_resource"])
            if parsed is not None:
                collision_bounds.append(parsed["bounds"])
                if primary_collision is None:
                    primary_collision = parsed
        region_bounds = None
        if collision_bounds:
            mins = [min(bounds["min"][axis] for bounds in collision_bounds) * scale for axis in range(3)]
            maxs = [max(bounds["max"][axis] for bounds in collision_bounds) * scale for axis in range(3)]
            region_bounds = {"min": mins, "max": maxs, "source": "collision headers"}
        type_counts = {}
        for item in resources:
            resource_type = item.get("resource_type") or item.get("status") or "unknown"
            type_counts[resource_type] = type_counts.get(resource_type, 0) + 1
        spawn_support = []
        if scene is not None and primary_collision is not None:
            for command in scene["commands"]:
                if command["id"] != 0:
                    continue
                for index, entry in enumerate(command["entries"]):
                    source_position = entry["position"]
                    position = coordinate(source_position, scale)
                    floor = collision_floor_at(primary_collision, *source_position)
                    if floor is not None:
                        floor["floor_y"] *= scale
                        floor["spawn_y_delta"] *= scale
                    spawn_support.append({"index": index, "position": position,
                                          "status": "supported" if floor else "no-nearby-floor",
                                          "floor": floor})
        selected_spawn = None
        if scene is not None:
            start_entries = next((command["entries"] for command in scene["commands"]
                                  if command["id"] == 0), [])
            supported_spawn = next((item for item in spawn_support
                                    if item["status"] == "supported"), None)
            if supported_spawn is not None:
                selected_spawn = {"position": supported_spawn["position"],
                                  "yaw": start_entries[supported_spawn["index"]]["rotation"][1]}
        native_include = output_dir / "render" / "hyrule_field_runtime.inc.c"
        native_summary = emit_native_level_include(
            native_include, room_meshes, decoded, texture_payloads, display_list_bodies,
            [entry["path"] for entry in files], endian_by_path, primary_collision,
            scale, selected_spawn)
        native_summary["include"] = str(native_include.relative_to(output_dir))
        native_summary["spawn_header"] = str((native_include.parent / "hyrule_field_spawn.h")
                                              .relative_to(output_dir))
        output = {
            "format": FORMAT_NAME,
            "format_version": FORMAT_VERSION,
            "region": {"id": "hyrule_field", "name": "Hyrule Field", "scene_resource": scene_path,
                       "scene_prefix": scene_prefix, "coordinate_system": {
                           "source_axes": {"x": "X", "y": "vertical Y", "z": "Z"},
                           "target_axes": {"x": "X", "y": "vertical Y", "z": "Z"},
                           "axis_transform": "provisional identity", "scale": scale,
                           "scale_calibration": "provisional; requires in-game comparison"}},
            "archive": {"name": archive_path.name, "sha256": sha256_file(archive_path),
                        "entry_count": len(files)},
            "resource_index": files,
            "dependencies": dependency_records,
            "scene": scene,
            "scene_resources": scene_nodes,
            "vertex_arrays": vertex_arrays,
            "textures": textures,
            "resources": resources,
            "collision_outputs": collision_outputs,
            "resource_type_counts": dict(sorted(type_counts.items())),
            "decoded_resource_count": sum(item.get("decode_status") == "complete" for item in resources),
            "parsed_resource_count": sum(bool(item.get("decoded")) for item in resources),
            "resource_type_counts_scope": "resources beneath the selected scene prefix",
            "vertex_array_count": len(vertex_arrays),
            "vertex_count": sum(len(array["vertices"]) for array in vertex_arrays),
            "texture_descriptor_count": len(textures),
            "texture_payload_decoded_count": len(render_data["texture_rgba8"]),
            "render_data": {key: value for key, value in render_data_json.items()
                            if key not in ("triangles", "display_lists", "vertex_arrays", "textures",
                                           "texture_records")},
            "render_manifest": "render/hyrule-render.json",
            "render_texture_records": render_data_json["texture_records"],
            "native_level_include": str(native_include.relative_to(output_dir)),
            "native_level_include_summary": native_summary,
            "region_bounds": region_bounds,
            "spawn_support": spawn_support,
            "rendering": {"status": "native-level-source-generated; device rendering unverified",
                          "reason": "Supported F3DEX2 lists are lowered to standard Gfx commands and N64 Collision "
                                    "data for the existing SM64 interpreter and physics path. Build with "
                                    "IOS_HYRULE_WORLD=1 and HYRULE_O2R_PATH to register it as a selectable level; "
                                    "this host conversion does not validate app compilation or device rendering"},
        }
        manifest = output_dir / "hyrule-world.json"
        manifest.write_text(json.dumps(output, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        return output


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="path to user-supplied oot.o2r ZIP archive")
    parser.add_argument("output", type=Path, help="directory for the intermediate world package")
    parser.add_argument("--scene-prefix", default="scenes/shared/spot00_scene")
    parser.add_argument("--scale", type=float, default=1.0, help="provisional OoT-to-SM64 coordinate scale")
    args = parser.parse_args(argv)
    try:
        result = import_archive(args.archive, args.output, args.scene_prefix, args.scale)
    except (ImportFailure, OSError) as exc:
        print(f"hyrule-importer: error: {exc}", file=sys.stderr)
        return 2
    decoded = result["decoded_resource_count"]
    parsed = result["parsed_resource_count"]
    print(f"Wrote {args.output / 'hyrule-world.json'}")
    print(f"Region: {result['region']['name']}; archive files: {result['archive']['entry_count']}; "
          f"resources parsed: {parsed}; fully decoded: {decoded}; "
          f"collision meshes: {len(result['collision_outputs'])}")
    if not result["scene"]["fully_decoded"]:
        print(f"Scene command decode stopped: {result['scene']['unsupported_at']}", file=sys.stderr)
    native = result["native_level_include_summary"]
    print(f"Lowered native SM64 data: {native['display_list_count']} Gfx lists, "
          f"{native['collision_triangles']} collision triangles; "
          f"{native['omitted_segmented_display_list_calls']} unresolved segmented calls explicitly omitted")
    print("The generated level data uses the existing SM64 Gfx and Collision interfaces; "
          "Xcode/device rendering is not validated by this host conversion.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
