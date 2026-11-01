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
        for path in sorted(selected):
            data = archive.read(index_by_name[path])
            header = read_resource_header(data, path)
            record = {"path": path, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(), **header}
            if header["status"] == "ok":
                body = data[OTR_HEADER_SIZE:]
                endian = "<" if header["endian"] == "little" else ">"
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
            "resources": resources,
            "collision_outputs": collision_outputs,
            "resource_type_counts": dict(sorted(type_counts.items())),
            "decoded_resource_count": sum(item.get("decode_status") == "complete" for item in resources),
            "parsed_resource_count": sum(bool(item.get("decoded")) for item in resources),
            "resource_type_counts_scope": "resources beneath the selected scene prefix",
            "region_bounds": region_bounds,
            "spawn_support": spawn_support,
            "rendering": {"status": "not converted", "reason":
                          "DisplayList, Array/vertex indirections, Texture payloads, and material state are indexed "
                          "but not decoded, and no runtime scene loader consumes this intermediate format"},
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
    print("World rendering/runtime loading is not included in this host importer milestone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
