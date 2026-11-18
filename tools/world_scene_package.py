#!/usr/bin/env python3
"""Write a WSC1 package from original, authored scene JSON."""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

MAX_BYTES = 4 * 1024 * 1024
TEXTURE_NAME = re.compile(r"scenes/[a-z0-9_-]+\.rgba16\Z")
U32_MAX = (1 << 32) - 1


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


def encode_scene(scene):
    scene_id = identifier(scene["id"], "scene ID", 1)
    rooms = array(scene["rooms"], 1, 16, "rooms")
    room_ids = unique_records(rooms, "room ID", 1)
    entrances = array(scene["entrances"], 1, 4096, "entrances")
    entrance_ids = unique_records(entrances, "entrance ID")
    exits = array(scene.get("exits", []), 0, 4096, "exits")
    unique_records(exits, "exit ID")
    output = bytearray(struct.pack("<4sI3H", b"WSC1", scene_id, len(rooms), len(entrances), len(exits)))
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
        render = array(room["render_vertices"], 3, 3 * 8192, "render vertices")
        if len(render) % 3:
            raise ValueError("render vertices must form complete triangles")
        collision = array(room["collision_vertices"], 3, 16384, "collision vertices")
        triangles = array(room["collision_triangles"], 1, 8192, "collision triangles")
        width, height, texture = 0, 0, b""
        if "texture" in room:
            record = room["texture"]
            width = integer(record["width"], 4, 2048, "texture width")
            height = integer(record["height"], 1, 512, "texture height")
            name = record["name"]
            if len(name) >= 96 or not TEXTURE_NAME.fullmatch(name) or width % 4 or width * height > 2048:
                raise ValueError("invalid room texture name or dimensions")
            texture = name.encode("ascii")
        append("<I6h7H", room["id"], *low, *high, len(neighbors), len(render) // 3,
               len(collision), len(triangles), width, height, len(texture))
        output.extend(texture)
        for neighbor in neighbors:
            append("<I", neighbor)
        for vertex in render:
            pos = vector(vertex["position"], -32768, 32767, 3, "render position")
            uv = vector(vertex.get("uv", [0, 0]), -32768, 32767, 2, "render UV")
            color = vector(vertex.get("color", [255, 255, 255, 255]), 0, 255, 4, "render color")
            if not inside(pos, low, high):
                raise ValueError("render vertex outside room bounds")
            append("<5h4B", *pos, *uv, *color)
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
    args = parser.parse_args()
    try:
        package = encode_scene(json.loads(args.input.read_text(encoding="utf-8")))
        args.output.write_bytes(package)
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f"world-scene-package: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
