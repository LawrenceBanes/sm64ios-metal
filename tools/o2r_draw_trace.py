#!/usr/bin/env python3
"""Bounded, host-only F3DEX2 draw-state tracing. Emits metadata, never assets."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from functools import lru_cache
import json
import math
from pathlib import Path
import struct
import sys
from zipfile import BadZipFile, ZipFile

if __package__:
    from . import fast3d_material, fast3d_pass_state, wsc_material_lowering
    from . import hyrule_importer as o2r
    from .o2r_inventory import archive_index
else:
    import fast3d_material
    import fast3d_pass_state
    import wsc_material_lowering
    import hyrule_importer as o2r
    from o2r_inventory import archive_index


MAX_RESOURCE_BYTES = 4 * 1024 * 1024
MAX_READ_BYTES = 64 * 1024 * 1024
MAX_ARCHIVE_FILE_BYTES = 128 * 1024 * 1024
MAX_LIST_COMMANDS = 4096
MAX_COMMANDS = 40_000
MAX_DRAWS = 10_000
MAX_CALL_DEPTH = 24
MAX_ROOMS = 64
MAX_MATRIX_DEPTH = 11
MAX_VERTICES = 64  # Pinned Fast3D window, not WSC's final packed vertex limit.
G_FOG = 0x00010000
G_LIGHTING = 0x00020000
G_TEXTURE_GEN = 0x00040000
G_TEXTURE_GEN_LINEAR = 0x00080000
_VERTEX_TYPES = ("Vertex", "Array")


class TraceError(Exception):
    def __init__(self, kind, reason):
        super().__init__(reason)
        self.kind = kind
        self.reported = False


@dataclass(frozen=True)
class Reference:
    path: str
    offset: int = 0


class ArchiveResources:
    """Reuse the catalog's ZIP and resource rules, reading only reached bodies."""

    def __init__(self, archive):
        self.archive = archive
        self.index = archive_index(archive)
        self.paths_by_hash = defaultdict(list)
        for path in sorted(self.index):
            if len(path) > 1024:
                raise o2r.ImportFailure("resource path exceeds trace length limit")
            self.paths_by_hash[o2r.crc64_resource_path(path)].append(path)
        self.bytes_read = 0
        self.accessed = {}
        self.body = lru_cache(maxsize=64)(self._body)
        self.read = lru_cache(maxsize=256)(self._read)

    def hashed(self, instruction, offset=0):
        key = instruction["resource_hash"]
        paths = self.paths_by_hash.get(int(key, 16), ())
        if len(paths) != 1:
            raise TraceError("unresolved", f"hash {key} has {len(paths)} matching paths")
        return Reference(paths[0], offset)

    def _body(self, path):
        record = self.accessed[path] = {"status": "missing"}
        if path not in self.index:
            raise TraceError("unresolved", f"resource path missing: {path}")
        info = self.index[path]
        record.update(status="rejected", bytes=info.file_size)
        if info.file_size > MAX_RESOURCE_BYTES or self.bytes_read + info.file_size > MAX_READ_BYTES:
            raise TraceError("limit", f"resource byte budget exceeded: {path}")
        try:
            data = self.archive.read(info)
        except (BadZipFile, RuntimeError, NotImplementedError) as error:
            raise TraceError("malformed", f"unreadable ZIP resource: {path}") from error
        self.bytes_read += len(data)
        header = o2r.read_resource_header(data, path)
        kind = header.get("resource_type")
        record.update(type=kind, version=header.get("version"))
        if header["status"] != "ok":
            raise TraceError("malformed", f"invalid resource header: {path}")
        if kind == "unknown":
            raise TraceError("unsupported", f"unrecognized resource type: {path}")
        version = header["version"]
        if version != 0 and (kind != "Texture" or version != 1):
            raise TraceError("unsupported", f"{kind} resource version {version}: {path}")

        body = data[header["body_offset"]:]
        endian = "<" if header["endian"] == "little" else ">"
        record["status"] = "header_read"
        return kind, version, endian, body

    def typed_body(self, path, expected):
        kind, version, endian, body = self.body(path)
        if kind not in expected:
            raise TraceError("malformed", f"expected {'/'.join(expected)}, found {kind}: {path}")
        return kind, version, endian, body

    def matrix(self, path, offset):
        kind, _, endian, body = self.body(path)
        if kind != "Matrix" or not body or len(body) % 64 or offset < 0 or offset % 64 or offset + 64 > len(body):
            raise TraceError("malformed", f"matrix reference is not aligned to a 64-byte matrix: {path}+{offset}")
        byteorder = "little" if endian == "<" else "big"
        integers = [int.from_bytes(body[offset + i * 2:offset + i * 2 + 2], byteorder, signed=True)
                    for i in range(16)]
        fractions = [int.from_bytes(body[offset + 32 + i * 2:offset + 34 + i * 2], byteorder, signed=False)
                     for i in range(16)]
        return tuple(tuple(integers[row * 4 + col] + fractions[row * 4 + col] / 65536.0
                           for col in range(4)) for row in range(4))

    def rsp_light(self, path, offset):
        """Read the 12-byte Light_t prefix copied by the pinned F3DEX2 handler."""
        kind, _, _, body = self.body(path)
        if kind != "Blob" or offset < 0 or offset + 12 > len(body):
            raise TraceError("malformed", f"MOVEMEM light is not a bounded 12-byte Blob record: {path}+{offset}")
        raw = body[offset:offset + 12]
        signed = lambda value: value - 256 if value >= 128 else value
        return {"color": list(raw[:3]), "direction": [signed(value) for value in raw[8:11]]}

    def _read(self, path, expected):
        kind, version, endian, body = self.typed_body(path, expected)
        try:
            if kind == "DisplayList":
                if not body:
                    raise TraceError("malformed", f"empty display list: {path}")
                if body[0] != 4:
                    raise TraceError("unsupported", f"display-list microcode {body[0]}: {path}")
                decoded = o2r.parse_display_list(body, endian, path, max_instructions=MAX_LIST_COMMANDS)
                if decoded["trailing_bytes"]:
                    raise TraceError("malformed", f"trailing bytes after ENDDL: {path}")
                commands = decoded["instructions"]
                commands.append({"offset": decoded["end_offset"], "opcode": 0xDF, "name": "ENDDL",
                                 "w0": 0xDF000000, "w1": 0})
                base = decoded["instruction_start"]
                return {command["offset"] - base: command for command in commands}
            if kind == "Scene":
                decoded = o2r.parse_scene(body, endian, path)
                if decoded["unsupported_at"]:
                    raise TraceError("unsupported", f"{path}: {decoded['unsupported_at']['reason']}")
                if (decoded["consumed_bytes"] != len(body) or
                        [c["id"] for c in decoded["commands"]][-1:] != [20]):
                    raise TraceError("malformed", f"incomplete scene command stream: {path}")
                return decoded["commands"]
            if kind in _VERTEX_TYPES:
                reader = o2r.Reader(body, endian, path)
                if kind == "Array" and reader.u32() != 25:
                    raise TraceError("unsupported", f"array element is not a vertex: {path}")
                count = reader.u32()
                if len(body) - reader.offset != count * 16:
                    raise TraceError("malformed", f"vertex count differs from byte size: {path}")
                return count
            if kind == "Matrix":
                if not body or len(body) % 64:
                    raise TraceError("malformed", f"matrix resource must contain complete 64-byte fixed-point matrices: {path}")
                return len(body) // 64
            texture = o2r.parse_texture_descriptor(body, endian, version, path)
            for key in ("horizontal_byte_scale", "vertical_pixel_scale"):
                if not math.isfinite(texture[key]) or texture[key] <= 0:
                    raise TraceError("malformed", f"invalid texture scale: {path}")
            return {key: texture[key] for key in ("format", "width", "height", "bits_per_pixel",
                                                 "payload_size", "flags", "horizontal_byte_scale",
                                                 "vertical_pixel_scale")}
        except o2r.ImportFailure as error:
            if f"exceeds {MAX_LIST_COMMANDS} commands" in str(error):
                raise TraceError("limit", str(error)) from error
            raise TraceError("malformed", str(error)) from error


def bits(word, shift, width):
    return (word >> shift) & ((1 << width) - 1)


def rgba(word):
    return [bits(word, shift, 8) for shift in (24, 16, 8, 0)]


def rect(w0, w1):
    return [bits(w0, 12, 12), bits(w0, 0, 12), bits(w1, 12, 12), bits(w1, 0, 12)]


def combiner(w0, w1):
    return [
        {"rgb": [bits(w0, 20, 4), bits(w1, 28, 4), bits(w0, 15, 5), bits(w1, 15, 3)],
         "alpha": [bits(w0, 12, 3), bits(w1, 12, 3), bits(w0, 9, 3), bits(w1, 9, 3)]},
        {"rgb": [bits(w0, 5, 4), bits(w1, 24, 4), bits(w0, 0, 5), bits(w1, 6, 3)],
         "alpha": [bits(w1, 21, 3), bits(w1, 3, 3), bits(w1, 18, 3), bits(w1, 0, 3)]},
    ]


def mux_textures(cycles):
    """Syntactic mux references. Does not simplify or evaluate a two-cycle combiner."""
    sources = set()
    for cycle in cycles:
        for key in ("rgb", "alpha"):
            for slot, selector in enumerate(cycle[key]):
                if selector in (1, 2):
                    sources.add(selector - 1)
                elif key == "rgb" and slot == 2 and selector in (8, 9):
                    sources.add(selector - 8)
    return sorted(sources)


def uses_color(cycles, color):
    rgb_alpha = {3: 10, 5: 12}[color]
    return any(color in cycle["rgb"] or color in cycle["alpha"] or cycle["rgb"][2] == rgb_alpha
               for cycle in cycles)


def axis_state(tile, axis, origin, limit, dimension):
    shift = tile[f"{axis}_shift"]
    mask = tile[f"{axis}_mask"]
    mode = tile[f"{axis}_mode"]
    return {
        "shift_raw": shift,
        "shift_multiplier": 2.0 ** (-shift if shift <= 10 else 16 - shift),
        "origin_10_2": origin, "limit_10_2": limit,
        "subtract_origin_texels": origin / 4,
        "normalization_dimension": dimension, "mask_period_texels": (1 << mask) if mask else None,
        "clamp": bool(mode & 2) or mode == 0 and mask == 0, "mirror": bool(mode & 1),
    }


def identity_matrix():
    return ((1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0),
            (0.0, 0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0))


def multiply_matrix(a, b):
    return tuple(tuple(sum(a[row][k] * b[k][col] for k in range(4)) for col in range(4))
                 for row in range(4))


class PassState:
    def __init__(self, segments):
        self.segments = dict(segments)
        self.vertices = [None] * MAX_VERTICES
        self.tiles = [{} for _ in range(8)]
        self.image = None
        self.loaded = {}
        self.palette_entries = {}
        self.texture = None
        self.geometry = self.geometry_known = 0
        self.other = [0, 0]
        self.known = [0, 0]
        self.combine = None
        self.colors = {}
        self.primitive_depth = None
        self.fog_factor = None
        self.num_directional_lights = None
        self.lights = {}
        self.tainted = set()
        self.approximations = set()
        self.matrix_stack = [identity_matrix()]

    def address(self, address):
        segment = address >> 24
        if not address & 1:
            raise TraceError("unresolved", f"unmapped direct address 0x{address:08x}")
        base = self.segments.get(segment)
        if base is None:
            raise TraceError("unresolved", f"unbound segment 0x{segment:02x}, address 0x{address:08x}")
        return Reference(base.path, base.offset + (address & 0x00FFFFFE))


class DrawTracer:
    def __init__(self, resources, scene, segments=None, profile=None, *, draw_sink=None, pass_preludes=None, room_segments=None):
        self.resources, self.scene = resources, scene
        self.segments = segments or {}
        self.profile = profile
        self.draw_sink = draw_sink
        self.pass_preludes = pass_preludes or {}
        self.room_segments = room_segments or {}
        self.generated = {}
        self.generated_calls = Counter()
        self.generated_uses = Counter()
        self.draws, self.issues = [], []
        self.decoded_lists, self.completed_lists, self.failed_lists = set(), set(), set()
        self.matrix_resources = set()
        self.roots_seen = self.commands = 0

    def issue(self, room, pass_name, path, offset, opcode, error):
        self.issues.append({"room": room, "pass": pass_name, "display_list": path, "offset": offset,
                            "command": opcode, "status": error.kind, "reason": str(error)})
        error.reported = True

    def command_source(self, path, offset):
        source = {"display_list": path, "offset": offset}
        if path in self.generated:
            spec, _ = self.generated[path]
            source.update(origin="reconstructed", source=spec.source)
            if spec.helper:
                source["helper"] = spec.helper
        return source

    def generated_list(self, pass_name, name, spec):
        path = f"@generated/{pass_name}/{name}"
        if path in self.resources.index:
            raise TraceError("malformed", f"archive resource collides with reserved generated path: {path}")
        if path not in self.generated:
            parsed = o2r.parse_display_list(spec.body(), ">", path, max_instructions=MAX_LIST_COMMANDS)
            base = parsed["instruction_start"]
            self.generated[path] = (spec, {ins["offset"] - base: {**ins, "offset": ins["offset"] - base}
                                           for ins in parsed["instructions"]})
        return Reference(path)

    def trace_pass(self, room, pass_name, roots, room_type=None):
        if not roots:
            return
        state = PassState({**self.segments, **self.room_segments.get(room, {})})
        if self.profile:
            prelude = self.profile.prelude(pass_name, room_type)
            for segment, spec in prelude.bindings.items():
                if segment in state.segments:
                    raise TraceError("malformed", f"explicit segment {segment:02x} conflicts with generated pass binding")
                state.segments[segment] = self.generated_list(pass_name, f"segment-{segment:02x}", spec)
            for index, spec in enumerate(prelude.commands):
                prelude_ref = self.generated_list(pass_name, f"prelude-{index}", spec)
                self.walk(room, pass_name, state, prelude_ref, ())
        if pass_name in self.pass_preludes:
            start_draws = len(self.draws)
            self.walk(room, pass_name, state, Reference(self.pass_preludes[pass_name]), ())
            if len(self.draws) != start_draws:
                raise TraceError("unsupported", f"{pass_name} prelude must contain only state commands")
        for path in roots:
            self.roots_seen += 1
            try:
                self.walk(room, pass_name, state, Reference(path), ())
            except TraceError as error:
                if error.kind == "limit":
                    raise
                # An aborted root can change state or draw order past the failure.
                # Resume at a later independently scheduled root with tainted state.
                state.tainted.add("incomplete command flow")
                self.failed_lists.add(path)

    def walk(self, room, pass_name, state, ref, stack):
        if len(stack) >= MAX_CALL_DEPTH:
            raise TraceError("limit", "display-list recursion depth exceeded")
        if ref in stack:
            raise TraceError("malformed", f"display-list call cycle at {ref.path}+{ref.offset}")
        try:
            listing = (self.generated[ref.path][1] if ref.path in self.generated
                       else self.resources.read(ref.path, ("DisplayList",)))
            if ref.offset not in listing:
                raise TraceError("malformed", f"display-list address is not a packet boundary: {ref.path}+{ref.offset}")
        except TraceError as error:
            self.issue(room, pass_name, ref.path, ref.offset, "display_list", error)
            self.failed_lists.add(ref.path)
            raise
        if ref.path not in self.generated:
            self.decoded_lists.add(ref.path)
        stack += (ref,)
        issues_at_entry = len(self.issues)
        for offset, ins in listing.items():
            if offset < ref.offset:
                continue
            self.commands += 1
            if self.commands > MAX_COMMANDS:
                raise TraceError("limit", "scene command budget exceeded")
            opcode, w0, w1 = ins["opcode"], ins["w0"], ins["w1"]
            if opcode == 0xDF:
                if len(self.issues) == issues_at_entry:
                    if ref.path not in self.generated:
                        self.completed_lists.add(ref.path)
                return
            try:
                if opcode in (0x31, 0xDE):
                    branch = bits(w0, 16, 8)
                    if branch not in (0, 1):
                        raise TraceError("malformed", "invalid display-list call mode")
                    if branch and opcode == 0x31:
                        raise TraceError("unsupported", "hashed tail branch is unsupported by the pinned handler")
                    target = self.resources.hashed(ins) if opcode == 0x31 else state.address(w1)
                    if target.path in self.generated:
                        self.generated_calls[target.path] += 1
                    self.walk(room, pass_name, state, target, stack)
                    if target.path in self.generated:
                        self.generated_uses[(room, pass_name, ref.path, ins["offset"], target.path)] += 1
                    if branch:
                        if len(self.issues) == issues_at_entry:
                            self.completed_lists.add(ref.path)
                        return
                elif opcode in (0x05, 0x06, 0x07):
                    self.draw(room, pass_name, state, ref.path, ins)
                else:
                    self.apply(state, ins, ref.path)
            except TraceError as error:
                if not error.reported:
                    self.issue(room, pass_name, ref.path, ins["offset"], ins["name"], error)
                self.failed_lists.add(ref.path)
                state.tainted.add(f"{error.kind}:{ins['name']}")
                # Calls can return to their known caller. A tail or unknown
                # control-flow opcode has no reliable continuation inside this list.
                if (error.kind == "limit" or opcode in (0x04, 0x35, 0xDD)
                        or opcode in (0x31, 0xDE) and bits(w0, 16, 8)
                        or ins["name"].startswith("UNKNOWN_")):
                    raise

    def apply(self, state, ins, path):
        opcode, w0, w1 = ins["opcode"], ins["w0"], ins["w1"]
        if opcode in (0x00, 0x33, 0xE6, 0xE7, 0xE8, 0xE9):
            return
        if opcode == 0x03:
            # The pinned Fast3D handler falls through unconditionally.
            state.approximations.add("CULLDL falls through as in the reference; visibility not evaluated")
        elif opcode in (0x01, 0x32):
            count = bits(w0, 12, 8)
            end = bits(w0, 1, 7)
            if not 0 < count <= end <= MAX_VERTICES:
                raise TraceError("malformed", "invalid vertex destination window")
            state.vertices[end - count:end] = [None] * count
            ref = self.resources.hashed(ins, w1) if opcode == 0x32 else state.address(w1)
            total = self.resources.read(ref.path, _VERTEX_TYPES)
            first, rem = divmod(ref.offset, 16)
            if rem or first + count > total:
                raise TraceError("malformed", f"invalid vertex source window: {ref.path}+{ref.offset}")
            scale = (state.texture["s_scale"], state.texture["t_scale"]) if state.texture else None
            matrix = state.matrix_stack[-1]
            state.vertices[end - count:end] = [
                (ref.path, first + i, scale, state.geometry & 0xF0000, matrix,
                 json.dumps(state.fog_factor, sort_keys=True) if state.fog_factor is not None else None,
                 json.dumps(state.num_directional_lights, sort_keys=True)
                 if state.num_directional_lights is not None else None,
                 json.dumps(state.lights, sort_keys=True)) for i in range(count)]
        elif opcode in (0x20, 0xFD):
            state.image = None
            ref = self.resources.hashed(ins) if opcode == 0x20 else state.address(w1)
            desc = self.resources.read(ref.path, ("Texture",))
            if ref.offset >= desc["payload_size"]:
                raise TraceError("malformed", "texture image reference is outside its payload")
            state.image = {"path": ref.path, "offset": ref.offset, "source": desc, "format": bits(w0, 21, 3),
                           "size_bits": 4 << bits(w0, 19, 2), "width": bits(w0, 0, 12) + 1}
        elif opcode == 0xDB:
            index, offset = bits(w0, 16, 8), bits(w0, 0, 16)
            if index == 6:
                segment, remainder = divmod(offset, 4)
                if remainder or segment >= 16:
                    raise TraceError("malformed", "invalid segment table destination")
                ref = state.address(w1)
                state.segments[segment] = ref
            elif index == 8:
                if offset != 0:
                    raise TraceError("unsupported", f"MOVEWORD fog offset 0x{offset:04x} is not supported")
                state.fog_factor = {"multiplier": struct.unpack(">h", struct.pack(">H", w1 >> 16))[0],
                                    "offset_coefficient": struct.unpack(">h", struct.pack(">H", w1 & 0xFFFF))[0],
                                    **self.command_source(path, ins["offset"])}
            elif index == 2:
                if offset != 0 or w1 % 24:
                    raise TraceError("malformed", "MOVEWORD NUMLIGHT must be 24-byte aligned at offset zero")
                state.num_directional_lights = {"count": w1 // 24,
                                                **self.command_source(path, ins["offset"])}
            else:
                raise TraceError("unsupported", f"MOVEWORD index 0x{index:02x} offset 0x{offset:04x} is not interpreted")
        elif opcode in (0x42, 0xDC):
            index = bits(w0, 0, 8)
            offset = bits(w0, 8, 8) * 8
            if index != 10 or offset < 48 or offset % 24:
                raise TraceError("unsupported", f"MOVEMEM index {index} offset 0x{offset:04x} is not a directional/ambient light slot")
            slot = offset // 24 - 2
            if not 0 <= slot <= 7:
                raise TraceError("unsupported", f"MOVEMEM light slot {slot} exceeds Fast3D bounds")
            ref = self.resources.hashed(ins) if opcode == 0x42 else state.address(w1)
            record = self.resources.rsp_light(ref.path, ref.offset)
            record.update(self.command_source(path, ins["offset"]))
            state.lights[slot] = record
        elif opcode == 0xD8:
            count, remainder = divmod(w1, 64)
            if remainder or count == 0 or count >= len(state.matrix_stack):
                raise TraceError("malformed", "POPMTX count underflows or is not a multiple of 64 bytes")
            del state.matrix_stack[-count:]
        elif opcode in (0xDA, 0x36):
            # Static F3DEX2 model-view matrices can be baked into world-space
            # vertices. Projection matrices depend on the runtime camera.
            parameters = (w0 & 0xFF) ^ 0x01  # Reverse gSPMatrix's F3DEX2 encoding.
            if parameters & ~0x07 or parameters & 0x04:
                raise TraceError("unsupported", "matrix projection or unknown F3DEX2 matrix flags cannot be baked into world geometry")
            ref = self.resources.hashed(ins) if opcode == 0x36 else state.address(w1)
            matrix = self.resources.matrix(ref.path, ref.offset)
            self.matrix_resources.add(ref.path)
            if parameters & 0x01:
                if len(state.matrix_stack) >= MAX_MATRIX_DEPTH:
                    raise TraceError("limit", "model-view matrix stack depth exceeded")
                state.matrix_stack.append(state.matrix_stack[-1])
            if parameters & 0x02:
                state.matrix_stack[-1] = matrix
            else:
                state.matrix_stack[-1] = multiply_matrix(matrix, state.matrix_stack[-1])
        elif opcode == 0xD7:
            state.texture = ins["texture_state"]
        elif opcode == 0xD9:
            clear = ins["geometry_clear_mask"]
            state.geometry = (state.geometry & ~clear) | w1
            state.geometry_known |= clear | w1
        elif opcode == 0xFC:
            state.combine = combiner(w0, w1)
        elif opcode in (0xF8, 0xF9, 0xFA, 0xFB):
            key = {0xF8: "fog", 0xF9: "blend", 0xFA: "primitive", 0xFB: "environment"}[opcode]
            state.colors[key] = {"rgba": rgba(w1), **self.command_source(path, ins["offset"])}
            if key == "primitive":
                state.colors[key].update(min_lod=bits(w0, 8, 8), lod_fraction=bits(w0, 0, 8))
        elif opcode == 0xEE:
            state.primitive_depth = [w1 >> 16, w1 & 0xFFFF]
        elif opcode in (0xE2, 0xE3):
            half = opcode - 0xE2
            length, shift = bits(w0, 0, 8) + 1, 31 - bits(w0, 8, 8) - bits(w0, 0, 8)
            if not 0 <= shift < 32 or length + shift > 32:
                raise TraceError("malformed", "invalid other-mode bit range")
            mask = ((1 << length) - 1) << shift
            state.other[half] = (state.other[half] & ~mask) | (w1 & mask)
            state.known[half] |= mask
        elif opcode == 0xEF:
            state.other = [w1, w0 & 0xFFFFFF]
            state.known = [0xFFFFFFFF, 0xFFFFFF]
        elif opcode == 0xF5:
            state.tiles[ins["tile"]].update(ins["tile_state"])
        elif opcode == 0xF2:
            uls, ult, lrs, lrt = rect(w0, w1)
            if lrs < uls or lrt < ult:
                raise TraceError("unsupported", "tile bounds cross the 10.2 coordinate wrap")
            state.tiles[ins["tile"]]["bounds_10_2"] = [uls, ult, lrs, lrt]
            state.tiles[ins["tile"]]["bounds_source"] = self.command_source(path, ins["offset"])
        elif opcode in (0xF0, 0xF3, 0xF4):
            self.load_texture(state, ins, path)
        else:
            raise TraceError("unsupported", f"state or command 0x{opcode:02x} has no trace interpretation")

    def load_texture(self, state, ins, path):
        opcode, w0, w1 = ins["opcode"], ins["w0"], ins["w1"]
        tile = state.tiles[bits(w1, 24, 3)]
        if state.image is None or "tmem_words" not in tile:
            raise TraceError("unresolved", "load has no resolved texture image or defined tile")
        image = state.image
        tmem = tile["tmem_words"]
        texel_bits = image["size_bits"]
        provenance = {"display_list": path, "offset": ins["offset"]}
        if opcode == 0xF0:
            count = bits(w1, 14, 10) + 1
            first = tmem - 256
            if texel_bits != 16 or not 0 <= first < first + count <= 256:
                raise TraceError("unsupported", "TLUT load lies outside supported 16-bit palette area")
            if image["offset"] + count * 2 > image["source"]["payload_size"]:
                raise TraceError("malformed", "TLUT load exceeds the texture payload")
            for entry in range(first, first + count):
                state.palette_entries[entry] = (image["path"], image["offset"], first, count, provenance)
            return

        uls, ult, lrs, lrt = rect(w0, w1)
        image_stride = (image["width"] * texel_bits + 7) // 8
        if opcode == 0xF3:
            if uls or ult:
                raise TraceError("unsupported", "nonzero LOADBLOCK origin")
            length = ((lrs + 1) * texel_bits + 7) // 8
            start = 0
            load = {"kind": "block", "total_bytes": length, "dxt_raw": lrt,
                    "source_stride_bytes": image_stride if image["width"] > 1 else None}
        else:
            if lrs < uls or lrt < ult:
                raise TraceError("malformed", "inverted LOADTILE extent")
            width, height = (lrs - uls) // 4 + 1, (lrt - ult) // 4 + 1
            if uls // 4 + width > image["width"]:
                raise TraceError("malformed", "LOADTILE exceeds source stride")
            start = (ult // 4) * image_stride + (uls // 4) * texel_bits // 8
            row = (width * texel_bits + 7) // 8
            length = (height - 1) * image_stride + row
            load = {"kind": "tile", "source_stride_bytes": image_stride, "row_bytes": row,
                    "width": width, "height": height, "bounds_10_2": [uls, ult, lrs, lrt],
                    "destination_stride_bytes": tile["line_words"] * 8}
            if texel_bits == 4 and (uls // 4) & 1 and width % 2 == 0:
                # An odd source X starts in the low nibble and the final
                # texel extends one byte beyond the nominal packed row span.
                length += 1
        descriptor = image["source"]
        if (descriptor["horizontal_byte_scale"], descriptor["vertical_pixel_scale"]) == (1.0, 1.0):
            if image["offset"] + start + length > descriptor["payload_size"]:
                raise TraceError("malformed", "texture load exceeds the recorded payload")
        else:
            load["approximation"] = "unscaled source range for replacement texture"
        load.update(image=image, tmem_words=tmem, source_offset=image["offset"] + start, **provenance)
        if self.draw_sink is not None:
            load["tmem_extent_bytes"] = length if opcode == 0xF3 else (
                (height - 1) * max(row, load["destination_stride_bytes"]) + row)
            for base, prior in list(state.loaded.items()):
                if (base != tmem and base * 8 < tmem * 8 + load["tmem_extent_bytes"]
                        and tmem * 8 < base * 8 + prior["tmem_extent_bytes"]):
                    # Neither partially overwritten source can be sampled as
                    # the original complete backing texture at the old base.
                    del state.loaded[base]
        state.loaded[tmem] = load

    def texture_input(self, state, number, missing, approximations, gaps):
        first = state.texture["tile"]
        # The reference forces both bindings to the base tile at higher LOD
        # bases. Retain that choice explicitly rather than reporting tile 8.
        index = first if number == 1 and first >= 2 else first + number
        if number == 1 and first >= 2:
            approximations.add("reference uses the same base tile for both inputs at base tile >= 2")
        tile = state.tiles[index]
        entry = {"input": number, "tile": index, "descriptor": dict(tile)}
        if "tmem_words" not in tile or "bounds_10_2" not in tile:
            missing.add(f"tile {index} descriptor or bounds absent")
            return entry
        load = state.loaded.get(tile["tmem_words"])
        if load is None:
            missing.add(f"tile {index} load unresolved at exact TMEM base")
            return entry
        entry["load"] = load
        if "approximation" in load:
            approximations.add(load["approximation"])
        descriptor = load["image"]["source"]
        uls, ult, lrs, lrt = tile["bounds_10_2"]
        width, height = (lrs - uls) // 4 + 1, (lrt - ult) // 4 + 1
        # Keep source dimensions, load stride and render extent distinct.
        # A WSC exporter must choose a backing extent after resolving TMEM.
        allocation = [descriptor["width"], descriptor["height"]]
        entry["uv"] = {
            "source_units": "s10.5", "tile_extent": [width, height],
            "s": axis_state(tile, "s", uls, lrs, allocation[0]),
            "t": axis_state(tile, "t", ult, lrt, allocation[1]),
            "half_texel_bias": (0.5 if bits(state.other[1], 12, 2) else 0.0)
            if state.known[1] & 0x3000 == 0x3000 else None,
            "normalization_basis": "source resource dimensions; TMEM extent requires lowering",
        }
        if any(axis["clamp"] or axis["mirror"] or (axis["mask_period_texels"] not in
                (None, axis["normalization_dimension"])) for axis in (entry["uv"]["s"], entry["uv"]["t"])):
            gaps.add("sampler clamp, mirror or independent mask period")
        if [width, height] != allocation or load["kind"] == "tile" or load["source_offset"]:
            if self.draw_sink is None:
                approximations.add("UV normalization uses resource extent; loaded subregions and TMEM aliases are not lowered")
            gaps.add("tile subregion or scrolling bounds need explicit lowering")
        if uls or ult:
            gaps.add("tile origin requires baking or a lowered scroll state")
        if tile["format"] == 2 and tile["size"] in (0, 1):
            first_entry, count = (tile["palette"] * 16, 16) if tile["size"] == 0 else (0, 256)
            loads = {}
            covered = state.palette_entries.keys() & range(first_entry, first_entry + count)
            for i in sorted(covered):
                resource_path, offset, start, amount, provenance = state.palette_entries[i]
                loads[resource_path, offset, start, amount, provenance["display_list"], provenance["offset"]] = {
                    "path": resource_path, "source_offset": offset, "first_entry": start, "entry_count": amount,
                    "load": provenance,
                }
            if len(covered) != count:
                missing.add(f"tile {index} palette entries missing ({len(covered)}/{count})")
            lut = bits(state.other[1], 14, 2)
            lut_known = state.known[1] & 0xC000 == 0xC000
            if not lut_known or lut not in (2, 3):
                if self.profile and lut_known and lut == 0:
                    # Both CI import routines in the pinned reference always
                    # unpack 5551. Keep the disabled RDP TLUT setting visible.
                    approximations.add("Fast3D CI decode uses RGBA5551 despite disabled RDP TLUT")
                else:
                    missing.add(f"tile {index} TLUT interpretation unspecified or disabled")
            if self.profile and lut_known and lut == 3:
                approximations.add("Fast3D CI decode uses RGBA5551 despite RDP IA88 TLUT")
            entry["palette"] = {"bank": tile["palette"] if count == 16 else None,
                                "interpretation": {2: "rgba5551", 3: "ia88"}.get(lut),
                                "first_entry": first_entry, "entry_count": count,
                                "loaded_entries": len(covered), "sources": list(loads.values())}
            if self.profile:
                entry["palette"]["raw_tlut"] = lut if lut_known else None
                entry["palette"]["reference_interpretation"] = "rgba5551"
        return entry

    def draw(self, room, pass_name, state, path, ins):
        if len(self.draws) >= MAX_DRAWS:
            raise TraceError("limit", "scene draw budget exceeded")
        w0, w1 = ins["w0"], ins["w1"]
        words = (w0,) if ins["opcode"] == 0x05 else (w0, w1)
        packed = [bits(word, shift, 8) for word in words for shift in (16, 8, 0)]
        if any(index & 1 or index // 2 >= MAX_VERTICES for index in packed):
            raise TraceError("malformed", "triangle vertex encoding is outside the supported window")
        source_ranges = defaultdict(set)
        missing, approximations, gaps = set(state.tainted), set(state.approximations), set()
        for slot in map(lambda i: i // 2, packed):
            vertex = state.vertices[slot]
            if not vertex:
                missing.add(f"vertex slot {slot} unavailable")
            else:
                source_ranges[vertex[0], vertex[2], vertex[3], vertex[4], vertex[5], vertex[6], vertex[7]].add(vertex[1])

        low, high = state.other
        if state.known[0] & 0xFFFF != 0xFFFF or state.known[1] & 0x30F000 != 0x30F000:
            missing.add("initial relevant other-mode bits not supplied")
        if not state.geometry_known & 1:
            missing.add("initial ZBUFFER geometry bit not supplied")
        cycle_type = bits(high, 20, 2) if state.known[1] & 0x300000 == 0x300000 else None
        cycles = []
        if state.combine is None:
            missing.add("combiner absent")
        elif cycle_type is None:
            cycles = state.combine
            approximations.add("cycle selection is unknown; texture inputs are potential references")
        else:
            cycles = state.combine[:cycle_type + 1] if cycle_type in (0, 1) else []
        syntactic = mux_textures(cycles)
        blender = fast3d_material.reference_blender(low) if state.known[0] == 0xFFFFFFFF else None
        material = fast3d_material.material_expression(cycles, cycle_type, blender and blender["alpha"])
        effective = self.profile or self.draw_sink is not None
        used = material["texture_inputs"] if effective and material else syntactic
        if cycle_type == 1 and not (effective and material):
            approximations.add("two-cycle mux selectors recorded; cycle aliasing and blend equations not evaluated")
            gaps.add("two-cycle combiner")
        elif cycle_type in (2, 3):
            missing.add("copy/fill cycle draw is outside the triangle state model")
        if len(used) == 2 or 1 in used:
            gaps.add("independent texture inputs")
        for name, selector, feature in (("environment", 5, "environment color is distinct from vertex tint"),
                                        ("primitive", 3, "primitive color or LOD constants")):
            if (any(value.startswith(name + ".") for value in material["inputs"])
                    if effective and material else uses_color(cycles, selector)):
                gaps.add(feature)
                if name not in state.colors:
                    missing.add(f"{name} combiner color absent")
        if effective and material:
            unresolved_inputs = {"combined.rgb", "combined.a", "key_center", "key_scale", "convert_k4", "convert_k5"}
            for name in sorted(unresolved_inputs.intersection(material["inputs"])):
                missing.add(f"active combiner input {name} is unavailable")
            if "lod_fraction" in material["inputs"]:
                missing.add("active combiner LOD fraction needs projected primitive evaluation")
            if "primitive_lod_fraction" in material["inputs"] and "primitive" not in state.colors:
                missing.add("primitive LOD fraction absent")
            if low & 4 and state.primitive_depth is None:
                missing.add("primitive depth value absent")
        if low & 3 or low & 4 or bits(low, 10, 2) == 3:
            gaps.add("alpha compare, decal depth or primitive depth")
        fog_enabled = any(geometry & G_FOG for _, _, geometry, *_ in source_ranges)
        vertex_fog_factors = sorted({factor if factor is not None else "null"
                                     for _, _, geometry, _, factor, _, _ in source_ranges if geometry & G_FOG})
        fog = {"enabled": fog_enabled,
               "vertex_factors": [json.loads(factor) for factor in vertex_fog_factors],
               "color": state.colors.get("fog") if fog_enabled else None,
               "depth_source": "runtime clip-space z/w" if fog_enabled else None}
        if fog_enabled:
            gaps.add("per-material clip-depth fog (WSC4)")
            if any(not (geometry & G_FOG) for _, _, geometry, _, _, _, _ in source_ranges):
                missing.add("one draw mixes fog-enabled and fog-disabled vertex loads")
            if "null" in vertex_fog_factors:
                missing.add("fog geometry mode is active but MOVEWORD fog coefficients are absent")
            if "fog" not in state.colors:
                missing.add("fog geometry mode is active but fog color is absent")
            if len(set(vertex_fog_factors) - {"null"}) > 1:
                missing.add("one draw uses multiple fog coefficient states")
        vertex_light_counts = sorted({count if count is not None else "null"
                                      for _, _, geometry, _, _, count, _ in source_ranges if geometry & G_LIGHTING})
        vertex_light_states = sorted({lights for _, _, geometry, _, _, _, lights in source_ranges
                                      if geometry & G_LIGHTING})
        lighting = {"enabled": bool(vertex_light_counts),
                    "shade_rgb_consumed": bool(material and "shade.rgb" in material.get("inputs", [])),
                    "directional_light_counts": [json.loads(count) for count in vertex_light_counts],
                    "light_records_at_vertex_load": [json.loads(lights) for lights in vertex_light_states]}
        if lighting["enabled"] and lighting["shade_rgb_consumed"]:
            gaps.add("vertex lighting")
            if "null" in vertex_light_counts:
                missing.add("lighting mode is active but MOVEWORD NUMLIGHT is absent")
            missing_lights = False
            for count in vertex_light_counts:
                if count == "null":
                    continue
                required = {str(index) for index in range(json.loads(count)["count"] + 1)}
                if any(not required.issubset(json.loads(light_state)) for light_state in vertex_light_states):
                    missing_lights = True
            if missing_lights:
                missing.add("lighting mode is active but directional or ambient MOVEMEM records are absent")
            else:
                gaps.add("static RSP vertex lighting baked into shade color")
        elif lighting["enabled"]:
            lighting["output_effect"] = "none; active combiner does not consume shade RGB"
        textures = []
        if used:
            if any(scale is None for _, scale, _, _, _, _, _ in source_ranges):
                missing.add("vertex UV scale not supplied at vertex load")
            if state.texture is None:
                missing.add("texture command absent for selected combiner")
            else:
                if not state.texture["enabled"]:
                    approximations.add("reference ignores the G_TEXTURE on field for combiner sampling")
                if state.texture["level"]:
                    approximations.add("LOD selection not evaluated")
                textures = [self.texture_input(state, i, missing, approximations, gaps) for i in used]

        # Persistent data are snapshotted. Resource bytes, triangles, vertex
        # values and texture payloads are deliberately absent from this schema.
        self.draws.append({
            "scene": self.scene, "room": room, "pass": pass_name, "order": len(self.draws),
            "display_list": path, "offset": ins["offset"], "command": ins["name"],
            "primitive_count": len(words), "geometry": {
                "mode": state.geometry, "known_mask": state.geometry_known,
                "vertex_ranges": [{"resource": source, "first": min(values), "last": max(values),
                                   "uv_scale_u0_16_at_load": scale, "geometry_at_vertex_load": geometry,
                                   "modelview_at_vertex_load": [list(row) for row in matrix],
                                   "fog_factor_at_vertex_load": json.loads(fog_factor) if fog_factor else None,
                                   "num_directional_lights_at_vertex_load": json.loads(num_lights) if num_lights else None,
                                   "light_records_at_vertex_load": json.loads(lights),
                                   "referenced_count": len(values)}
                                  for (source, scale, geometry, matrix, fog_factor, num_lights, lights), values in sorted(source_ranges.items(),
                                      key=lambda entry: (entry[0][0], entry[0][1] or (-1, -1), entry[0][2]))]},
            "textures": textures, "combiner": {"cycle_type": cycle_type, "selectors": state.combine,
                                               "colors": dict(state.colors), "mux_texture_inputs": syntactic,
                                               "effective": material},
                "fog": fog, "lighting": lighting,
            "render_state": {"other_mode_low": low, "other_mode_high": high, "known_masks": list(state.known),
                             "depth_test": bool((state.geometry & 1 or low & 4) and low & 0x10)
                             if state.known[0] & 0x14 == 0x14 and state.geometry_known & 1 else None,
                             "depth_write": bool(low & 0x20) if state.known[0] & 0x20 else None,
                             "z_mode": bits(low, 10, 2) if state.known[0] & 0xC00 == 0xC00 else None,
                             "primitive_depth": state.primitive_depth,
                             "alpha_compare": low & 3 if state.known[0] & 3 == 3 else None,
                             "force_blend": bool(low & 0x4000) if state.known[0] & 0x4000 else None,
                             "blender_raw": low >> 16 if state.known[0] >> 16 == 0xFFFF else None,
                             "reference_blender": blender},
            "status": "incomplete" if missing else "approximate" if approximations else "complete",
            "missing": sorted(missing), "approximations": sorted(approximations),
            "wsc2_gaps": sorted(gaps),
        })
        if self.draw_sink is not None:
            self.draw_sink(self.draws[-1], [state.vertices[i // 2] for i in packed], state.palette_entries)

    def run(self):
        budget_error = None
        rooms = []
        try:
            scene_commands = self.resources.read(self.scene, ("Scene",))
            room_list = [room["path"] for cmd in scene_commands if cmd["id"] == 4 for room in cmd["rooms"]]
            if len(room_list) > MAX_ROOMS:
                raise TraceError("limit", "scene room count exceeds the trace limit")
            for room in room_list or [self.scene]:
                try:
                    commands = self.resources.read(room, ("Scene",))
                except TraceError as error:
                    if error.kind == "limit":
                        raise
                    self.issue(room, None, room, None, "room_header", error)
                    continue
                room_types = [command["gameplay_flags"] for command in commands if command["id"] == 8]
                room_type = room_types[-1] if room_types else None
                for command in commands:
                    if command["id"] != 10:
                        continue
                    mesh = command["mesh"]
                    if mesh["type"] != 0:
                        self.issue(room, None, room, command["offset"], "room_mesh",
                                   TraceError("unsupported", "only normal room meshes have a verified submission order"))
                        continue
                    rooms.append(room)
                    for pass_name in ("opaque", "translucent"):
                        roots = [entry[f"{pass_name}_display_list"] for entry in mesh["polygons"]
                                 if entry[f"{pass_name}_display_list"]]
                        self.trace_pass(room, pass_name, roots, room_type)
        except TraceError as error:
            if not error.reported:
                self.issue(None, None, self.scene, None, "scene_trace", error)
            budget_error = str(error) if error.kind == "limit" else None
        return {
            "format": "fast3d-draw-state-trace", "version": 2, "scene": self.scene, "rooms": rooms,
            "profile": {"name": "oot-ntsc10-kokiri-normal-room", "inputs": asdict(self.profile),
                        "sources": [fast3d_pass_state.SCENE_ORDER, fast3d_pass_state.ROOM_DRAW],
                        "scope": "independent room and pass snapshots; camera, lights, fog parameters and previous room memory are not evaluated"}
            if self.profile else None,
            "generated_lists": {path: {"source": spec.source, "helper": spec.helper,
                                      "packets": [list(pair) for pair in spec.packets],
                                      "called": self.generated_calls[path]}
                                for path, (spec, _) in sorted(self.generated.items())},
            "generated_calls": [{"room": room, "pass": pass_name, "caller": path, "offset": offset,
                                 "target": target, "executions": count}
                                for (room, pass_name, path, offset, target), count in sorted(self.generated_uses.items())],
            "ordering": "room-table order, then opaque and translucent source order within each room",
            "coverage": {"commands": self.commands, "mesh_roots_attempted": self.roots_seen,
                         "decoded_display_lists": sorted(self.decoded_lists),
                         "lists_completed_without_issue": sorted(self.completed_lists - self.failed_lists),
                         "lists_with_issues": sorted(self.failed_lists),
                         "draws": dict(sorted(Counter(draw["status"] for draw in self.draws).items())),
                         "interpreted_state_resolved": sum(not draw["missing"] for draw in self.draws),
                         "generated_segment_calls_resolved": sum(self.generated_uses.values()),
                         "static_matrix_resources": sorted(self.matrix_resources),
                         "issues": dict(sorted(Counter(issue["status"] for issue in self.issues).items())),
                         "wsc2_gaps": dict(sorted(Counter(gap for d in self.draws for gap in d["wsc2_gaps"]).items())),
                         "budget_error": budget_error},
            "issues": self.issues, "draws": self.draws,
        }


def trace_archive(archive_path, scene, segments=None, profile=None):
    archive_path = Path(archive_path)
    if archive_path.stat().st_size > MAX_ARCHIVE_FILE_BYTES:
        raise o2r.ImportFailure("archive file exceeds trace size limit")
    with ZipFile(archive_path) as archive:
        if profile:
            try:
                with archive.open("version") as version:
                    marker = version.read(len(fast3d_pass_state.NTSC_10) + 1)
            except KeyError:
                marker = b""
            if marker != fast3d_pass_state.NTSC_10 or scene != fast3d_pass_state.KOKIRI_SCENE:
                raise ValueError("the Kokiri reconstruction requires its exact scene and OoT NTSC 1.0 marker")
        return DrawTracer(ArchiveResources(archive), scene, segments, profile).run()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("scene", help="exact path of scene resource")
    parser.add_argument("--segment", action="append", default=[], metavar="ID=RESOURCE",
                        help="explicit alias for a segment, in hexadecimal")
    parser.add_argument("--kokiri-context", nargs=4, type=int, metavar=("FRAME", "LAYER", "EVENT07", "DRAW_PARAM0"),
                        help="use the pinned Kokiri pass model with explicit runtime scenario inputs (EVENT07 0 or 1)")
    parser.add_argument("--lower-wsc2", action="store_true", help="classify each draw against WSC2")
    parser.add_argument("--wsc2-filter", choices=("point", "bilerp"),
                        help="known inherited WSC2 filtering for the comparison")
    parser.add_argument("--output", required=True, type=Path, help="write the metadata trace to this local file")
    args = parser.parse_args(argv)
    try:
        if (args.output.resolve() == args.archive.resolve() or
                args.output.exists() and args.output.samefile(args.archive)):
            raise ValueError("trace output must be separate from the input archive")
        segments = {}
        for binding in args.segment:
            segment, path = binding.split("=", 1)
            key = int(segment, 16)
            if not 0 <= key < 16 or key in segments or not path:
                raise ValueError(f"invalid or duplicate segment binding: {binding}")
            segments[key] = Reference(o2r.safe_archive_path(path))
        profile = None
        if args.kokiri_context is not None:
            frame, layer, event07, draw_param0 = args.kokiri_context
            if event07 not in (0, 1):
                raise ValueError("EVENT07 must be 0 or 1")
            profile = fast3d_pass_state.KokiriInputs(frame, layer, bool(event07), draw_param0)
        report = trace_archive(args.archive, o2r.safe_archive_path(args.scene), segments, profile)
        if args.lower_wsc2 or args.wsc2_filter:
            wsc_material_lowering.lower_report(report, {None: None, "point": 0, "bilerp": 2}[args.wsc2_filter])
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
    except (OSError, ValueError, BadZipFile, o2r.ImportFailure) as error:
        parser.exit(1, f"trace failed: {error}\n")
    coverage = report["coverage"]
    print(f"{len(report['rooms'])} rooms, {len(coverage['decoded_display_lists'])} decoded lists, "
          f"{len(coverage['lists_completed_without_issue'])} completed without issues")
    print("Draws", coverage["draws"], "Issues", coverage["issues"])
    if "wsc2" in report:
        print("WSC2 feasibility", report["wsc2"]["classifications"])
    for feature, count in coverage["wsc2_gaps"].items():
        print(f"  WSC2 gap: {feature} ({count} draw records)")
    return int(bool(report["issues"] or coverage["draws"].get("incomplete") or not report["draws"]))


if __name__ == "__main__":
    sys.exit(main())
