#!/usr/bin/env python3
"""Write a WSC1–WSC5 package from original, authored scene JSON."""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

if __package__:
    from .hyrule_importer import write_rgba_png
else:
    from hyrule_importer import write_rgba_png

MAX_BYTES = 4 * 1024 * 1024
TEXTURE_NAME = re.compile(r"scenes/[a-z0-9_-]+\.rgba16\Z")
U32_MAX = (1 << 32) - 1
MAX_TEXTURE_TEXELS_LEGACY = 2048
MAX_TEXTURE_TEXELS_WSC5 = 4096


def integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{label} must be an integer in [{minimum}, {maximum}]")
    return value


def array(value, minimum, maximum, label):
    if not isinstance(value, list) or not minimum <= len(value) <= maximum:
        raise ValueError(f"{label} must contain {minimum} to {maximum} items")
    return value


def vector(values, minimum, maximum, length, label):
    return [integer(n, minimum, maximum, label) for n in array(values, length, length, label)]


def identifier(value, label, minimum=0):
    return integer(value, minimum, U32_MAX, label)


def unique_records(records, label, minimum=0):
    ids = [identifier(record["id"], label, minimum) for record in records]
    if len(set(ids)) != len(ids):
        raise ValueError(f"duplicate {label}")
    return set(ids)


def texture_descriptor(record, version=1):
    if record is None:
        return 0, 0, b""
    width = integer(record["width"], 4, 2048, "texture width")
    height = integer(record["height"], 1, 512, "texture height")
    name = record["name"]
    max_texels = MAX_TEXTURE_TEXELS_WSC5 if version >= 5 else MAX_TEXTURE_TEXELS_LEGACY
    if len(name) >= 96 or not TEXTURE_NAME.fullmatch(name) or width % 4 or width * height > max_texels:
        raise ValueError("invalid scene texture name or dimensions")
    return width, height, name.encode("ascii")


def decode_indexed_rgba(record, version=1):
    """Expand packed CI4/CI8 using an explicit RGBA5551 or IA88 palette."""
    width, height, _ = texture_descriptor(record, version)
    bits = integer(record["index_bits"], 4, 8, "index bits")
    if bits not in (4, 8):
        raise ValueError("only four- and eight-bit indices are supported")
    raw_indices = bytes.fromhex(record["indices"])
    raw_palette = bytes.fromhex(record["palette"])
    if len(raw_indices) != width * height * bits // 8:
        raise ValueError("packed index byte count does not match dimensions")
    if not 2 <= len(raw_palette) <= 2 * (1 << bits) or len(raw_palette) % 2:
        raise ValueError("palette byte count exceeds the index format or contains a partial entry")

    fmt = record["palette_format"]
    if fmt not in ("rgba5551", "ia88"):
        raise ValueError("unsupported palette format")
    palette = []
    for i in range(0, len(raw_palette), 2):
        high, low = raw_palette[i:i + 2]
        if fmt == "ia88":
            palette.append(bytes((high, high, high, low)))
        else:
            packed = high << 8 | low
            rgb = [(packed >> shift) & 31 for shift in (11, 6, 1)]
            palette.append(bytes([((component << 3) | (component >> 2)) for component in rgb]
                                 + [255 if packed & 1 else 0]))

    pixels = bytearray()
    for i in range(width * height):
        index = raw_indices[i] if bits == 8 else (raw_indices[i // 2] >> (0 if i % 2 else 4)) & 15
        if index >= len(palette):
            raise ValueError(f"palette index {index} at texel {i} is out of range")
        pixels.extend(palette[index])
    return bytes(pixels)


def indexed_textures(scene):
    assets = {}
    version = scene.get("version", 1)
    for record in array(scene.get("indexed_textures", []), 0, 1024, "indexed textures"):
        width, height, name = texture_descriptor(record, version)
        if name in assets:
            raise ValueError("duplicate indexed texture name")
        assets[name] = (width, height, decode_indexed_rgba(record, version))
    for room in scene["rooms"]:
        for batch in room.get("batches", [room]):
            bindings = batch["material"].get("textures", []) if scene.get("version") in (3, 4, 5) else [batch.get("texture")]
            for binding in bindings:
                width, height, name = texture_descriptor(binding, version)
                if name in assets and assets[name][:2] != (width, height):
                    raise ValueError("material dimensions differ from its indexed texture")
    return assets


def encode_material(append, output, batch, draw_pass, triangles, version):
    if set(batch) - {"material", "render_vertices", "pass"}:
        raise ValueError("unsupported WSC3 batch field")
    material = batch["material"]
    fields = {"depth_test", "depth_write", "cutout", "cull_back", "primitive", "environment",
              "alpha_period", "alpha_peak", "textures"}
    if version >= 4:
        fields.add("fog")
    elif "fog" in material:
        raise ValueError("per-material camera-depth fog requires WSC4")
    if version >= 5:
        fields.add("decal")
        fields.add("blend")
    elif "decal" in material:
        raise ValueError("per-material decal depth bias requires WSC5")
    elif "blend" in material:
        raise ValueError("per-material blend selection requires WSC5")
    if set(material) - fields:
        raise ValueError("unsupported WSC3 material field")
    translucent = draw_pass == "translucent"
    blend = material.get("blend", translucent) if version >= 5 else translucent
    if type(blend) is not bool:
        raise ValueError("blend must be Boolean")
    flags = int(blend) << 4
    for i, (key, default) in enumerate((("depth_test", True), ("depth_write", not translucent),
                                        ("cutout", False), ("cull_back", False))):
        value = material.get(key, default)
        if type(value) is not bool:
            raise ValueError(f"{key} must be Boolean")
        flags |= int(value) << i
    if translucent and flags & 4:
        raise ValueError("WSC3 cutout requires the opaque pass")
    fog = material.get("fog")
    if fog is not None:
        if version < 4:
            raise ValueError("per-material camera-depth fog requires WSC4")
        if not isinstance(fog, dict) or set(fog) != {"multiplier", "offset", "color"}:
            raise ValueError("fog requires multiplier, offset, and RGBA color")
        multiplier = integer(fog["multiplier"], -32768, 32767, "fog multiplier")
        fog_offset = integer(fog["offset"], -32768, 32767, "fog offset")
        fog_color = vector(fog["color"], 0, 255, 4, "fog RGBA")
        flags |= 32
    elif version >= 4:
        multiplier = fog_offset = 0
        fog_color = [0, 0, 0, 0]
    if version >= 5:
        decal = material.get("decal", False)
        if type(decal) is not bool:
            raise ValueError("decal must be Boolean")
        if (translucent and not blend) or (blend and not translucent and not decal):
            raise ValueError("WSC5 permits opaque-list blending only for decal materials")
        if decal and not blend:
            raise ValueError("WSC5 decal materials must enable blending")
        flags |= int(decal) << 6
    primitive = vector(material.get("primitive", [255] * 4), 0, 255, 4, "primitive RGBA")
    environment = vector(material.get("environment", [255, 255, 255, 0]), 0, 255, 4, "environment RGBA")
    period = integer(material.get("alpha_period", 0), 0, 65535, "environment alpha period")
    peak = integer(material.get("alpha_peak", environment[3]), 0, 255, "environment alpha peak")
    if period == 1:
        raise ValueError("environment alpha period must be zero or at least two")
    append("<3H8B2H", translucent, triangles, flags, *primitive, *environment, period, peak)
    textures = array(material.get("textures", []), 0, 2, "material bindings")
    address = {"repeat": 0, "mirror": 1, "clamp": 2}
    if any(binding is None for binding in textures):
        raise ValueError("WSC3 texture bindings must be contiguous")
    for binding in [*textures, *([None] * (2 - len(textures)))]:
        width, height, name = texture_descriptor(binding, version)
        binding = binding or {}
        if set(binding) - {"name", "width", "height", "address", "filter", "scale", "origin", "scroll"}:
            raise ValueError("unsupported WSC3 texture field")
        modes = array(binding.get("address", ["repeat", "repeat"]), 2, 2, "address modes")
        filter_mode = binding.get("filter", "point")
        if any(mode not in address for mode in modes) or filter_mode not in ("point", "three_point"):
            raise ValueError("unsupported WSC3 sampler")
        sampler = address[modes[0]] | address[modes[1]] << 2 | (filter_mode == "three_point") << 4
        scale = vector(binding.get("scale", [256, 256]), -32768, 32767, 2, "Q8.8 UV scale")
        origin = vector(binding.get("origin", [0, 0]), -32768, 32767, 2, "UV origin")
        scroll = vector(binding.get("scroll", [0, 0]), -32768, 32767, 2, "UV scroll")
        append("<3H6hH", width, height, sampler, *scale, *origin, *scroll, len(name))
        output.extend(name)
    if version >= 4 and fog is not None:
        append("<2h4B", multiplier, fog_offset, *fog_color)


def encode_scene(scene):
    scene_id = identifier(scene["id"], "scene ID", 1)
    rooms = array(scene["rooms"], 1, 16, "rooms")
    room_ids = unique_records(rooms, "room ID", 1)
    entrances = array(scene["entrances"], 1, 4096, "entrances")
    entrance_ids = unique_records(entrances, "entrance ID")
    exits = array(scene.get("exits", []), 0, 4096, "exits")
    unique_records(exits, "exit ID")
    version = integer(scene.get("version", 1), 1, 5, "scene version")
    magic = (b"WSC1", b"WSC2", b"WSC3", b"WSC4", b"WSC5")[version - 1]
    output = bytearray(struct.pack("<4sI3H", magic, scene_id, len(rooms), len(entrances), len(exits)))
    bounds = {}

    def append(fmt, *values):
        if len(output) + struct.calcsize(fmt) > MAX_BYTES:
            raise ValueError("scene package exceeds the 4 MiB limit")
        output.extend(struct.pack(fmt, *values))

    for room in rooms:
        low = vector(room["min"], -32768, 32767, 3, "room min")
        high = vector(room["max"], -32768, 32767, 3, "room max")
        if any(a > b for a, b in zip(low, high)):
            raise ValueError("inverted room bounds")
        bounds[room["id"]] = (low, high)
        neighbors = array(room.get("neighbors", []), 0, 16, "room neighbors")
        neighbors = [identifier(n, "neighbor") for n in neighbors]
        if room["id"] in neighbors or len(set(neighbors)) != len(neighbors) or set(neighbors) - room_ids:
            raise ValueError("invalid room neighbors")
        collision = array(room["collision_vertices"], 3, 16384, "collision vertices")
        triangles = array(room["collision_triangles"], 1, 8192, "collision triangles")

        def render_vertices(vertices):
            for vertex in vertices:
                pos = vector(vertex["position"], -32768, 32767, 3, "render position")
                uv = vector(vertex.get("uv", [0, 0]), -32768, 32767, 2, "render UV")
                color = vector(vertex.get("color", [255, 255, 255, 255]), 0, 255, 4, "render color")
                if not inside(pos, low, high):
                    raise ValueError("render vertex outside room bounds")
                append("<5h4B", *pos, *uv, *color)

        def read_render(batch):
            render = array(batch["render_vertices"], 3, 3 * 8192, "render vertices")
            if len(render) % 3:
                raise ValueError("render vertices must form complete triangles")
            return render

        if version == 1:
            if "batches" in room or "states" in room:
                raise ValueError("material batches and draw-state slots require scene version 2")
            render = read_render(room)
            width, height, texture = texture_descriptor(room.get("texture"), version)
            append("<I6h7H", room["id"], *low, *high, len(neighbors), len(render) // 3,
                   len(collision), len(triangles), width, height, len(texture))
            output.extend(texture)
            for neighbor in neighbors:
                append("<I", neighbor)
            render_vertices(render)
        else:
            if "render_vertices" in room or "texture" in room:
                raise ValueError("scene version 2 stores rendering in batches")
            max_batches = 256 if version >= 5 else 64
            batches = array(room["batches"], 1, max_batches, "batches")
            states = array(room.get("states", []), 0, 64, "draw-state slots")
            if version in (3, 4, 5) and states:
                raise ValueError("WSC3/WSC4/WSC5 store draw state per material")
            append("<I6h5H", room["id"], *low, *high, len(neighbors), len(batches),
                   len(collision), len(triangles), len(states))
            for neighbor in neighbors:
                append("<I", neighbor)
            for state in states:
                scroll = vector(state.get("scroll", [0, 0]), -32768, 32767, 2, "scroll rates")
                tint = vector(state.get("tint", [255, 255, 255, 255]), 0, 255, 4, "tint")
                append("<2h4B", *scroll, *tint)
            total = 0
            for batch in batches:
                render = read_render(batch)
                total += len(render) // 3
                if total > 8192:
                    raise ValueError("combined room rendering exceeds the triangle limit")
                draw_pass = batch.get("pass", "opaque")
                if draw_pass not in ("opaque", "translucent"):
                    raise ValueError("invalid scene render pass")
                if version in (3, 4, 5):
                    encode_material(append, output, batch, draw_pass, len(render) // 3, version)
                else:
                    state = integer(batch.get("state", 0), 0, len(states), "draw-state slot")
                    width, height, texture = texture_descriptor(batch.get("texture"), version)
                    append("<6H", draw_pass == "translucent", state, len(render) // 3, width, height, len(texture))
                    output.extend(texture)
                render_vertices(render)
        for vertex in collision:
            pos = vector(vertex, -32768, 32767, 3, "collision position")
            if not inside(pos, low, high):
                raise ValueError("collision vertex outside room bounds")
            append("<3h", *pos)
        for triangle in triangles:
            indices = vector(triangle["indices"], 0, len(collision) - 1, 3, "collision index")
            if len(set(indices)) != 3:
                raise ValueError("collision triangle has duplicate indices")
            terrain = integer(triangle.get("type", 0), 0, 32767, "surface type")
            force = integer(triangle.get("force", 0), -32768, 32767, "surface force")
            append("<3H2h", *indices, terrain, force)

    for entrance in entrances:
        room_id = identifier(entrance["room"], "entrance room", 1)
        pos = vector(entrance["position"], -32768, 32767, 3, "entrance position")
        yaw = integer(entrance.get("yaw", 0), -32768, 32767, "entrance yaw")
        if room_id not in bounds or not inside(pos, *bounds[room_id]):
            raise ValueError("entrance outside its room")
        append("<2I4h", entrance["id"], room_id, *pos, yaw)
    for exit_ in exits:
        destination = identifier(exit_["scene"], "exit scene", 1)
        entrance = identifier(exit_["entrance"], "exit entrance")
        if destination == scene_id and entrance not in entrance_ids:
            raise ValueError("exit references unknown local entrance")
        append("<3I", exit_["id"], destination, entrance)
    return bytes(output)


def inside(position, low, high):
    return all(a <= n <= b for n, a, b in zip(position, low, high))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--texture-root", type=Path, help="Documents root for authored indexed PNGs")
    args = parser.parse_args()
    try:
        scene = json.loads(args.input.read_text(encoding="utf-8"))
        package = encode_scene(scene)
        textures = indexed_textures(scene)
        if textures and args.texture_root is None:
            raise ValueError("indexed textures require --texture-root")
        for name, (width, height, pixels) in textures.items():
            write_rgba_png(args.texture_root / "gfx" / (name.decode("ascii") + ".png"), width, height, pixels)
        args.output.write_bytes(package)
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f"world-scene-package: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
