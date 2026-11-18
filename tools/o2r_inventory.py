#!/usr/bin/env python3
"""Inventory O2R resources and scene links without exporting game assets."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path, PurePosixPath
import sys
from zipfile import BadZipFile, ZipFile

if __package__:
    from . import hyrule_importer as o2r
else:
    import hyrule_importer as o2r


MAX_ENTRIES = 100_000
MAX_ENTRY_BYTES = 16 * 1024 * 1024
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
PARSER_TYPES = {"Scene", "Array", "CollisionHeader", "DisplayList", "Texture"}
HASH_REFERENCE_TYPES = {
    0x20: {"Texture"},
    0x31: {"DisplayList"},
    0x32: {"Array", "Vertex"},
    0x35: {"DisplayList"},
    0x36: {"Matrix"},
    0x42: None,
}


def archive_index(archive: ZipFile) -> dict:
    index = {}
    total_bytes = 0
    for entry in archive.infolist():
        if entry.is_dir():
            continue
        path = o2r.safe_archive_path(entry.filename)
        if path in index:
            raise o2r.ImportFailure(f"duplicate ZIP resource path: {path}")
        index[path] = entry
        total_bytes += entry.file_size
        if (entry.file_size > MAX_ENTRY_BYTES or total_bytes > MAX_ARCHIVE_BYTES
                or len(index) > MAX_ENTRIES):
            raise o2r.ImportFailure("O2R archive exceeds inventory limits")
    return index


def scene_metadata(body: bytes, endian: str, path: str) -> dict:
    scene = o2r.parse_scene(body, endian, path)
    rooms, entrances, transitions, exits, alternates, meshes = [], [], [], [], [], []
    spawn_count = 0
    for command in scene["commands"]:
        command_id = command["id"]
        if command_id == 0:
            spawn_count = len(command["entries"])
        elif command_id == 4:
            rooms.extend(room["path"] for room in command["rooms"])
        elif command_id == 6:
            entrances.extend(command["entrances"])
        elif command_id == 10:
            mesh = command["mesh"]
            meshes.append({
                "type": mesh["type"],
                "opaque_lists": sum(polygon["opaque_display_list"] is not None for polygon in mesh["polygons"]),
                "translucent_lists": sum(polygon["translucent_display_list"] is not None
                                         for polygon in mesh["polygons"]),
                "background_count": len(mesh["background_images"]),
            })
        elif command_id == 14:
            transitions.extend({key: entry[key] for key in ("front_room", "front_effect",
                                                           "back_room", "back_effect")}
                               for entry in command["transition_actors"])
        elif command_id == 19:
            exits.extend(command["exits"])
        elif command_id == 24:
            alternates.extend(command["alternate_headers"])
    return {
        "command_ids": [command["id"] for command in scene["commands"]],
        "command_count": scene["command_count"],
        "unsupported_at": scene["unsupported_at"],
        "trailing_bytes": len(body) - scene["consumed_bytes"],
        "dependencies": scene["dependencies"],
        "rooms": rooms,
        "spawn_count": spawn_count,
        "entrances": entrances,
        "transitions": transitions,
        "exit_ids": exits,
        "alternate_headers": alternates,
        "meshes": meshes,
    }


def summarize_resource(record: dict, body: bytes, hash_index: dict, resources: dict) -> None:
    path = record["path"]
    kind = record["type"]
    endian = "<" if record["endian"] == "little" else ">"
    version = record["version"]
    if version != 0 and (kind != "Texture" or version != 1):
        record.update(status="unsupported", reason=f"unsupported {kind} version {version}")
        return

    limitations = set()
    if kind == "Scene":
        scene = scene_metadata(body, endian, path)
        record["scene"] = scene
        record["dependencies"] = scene.pop("dependencies")
        if scene["unsupported_at"] or scene["trailing_bytes"] or scene["command_ids"][-1:] != [20]:
            limitations.add("scene command stream not fully decoded")
    elif kind == "CollisionHeader":
        collision = o2r.parse_collision(body, endian, path)
        record["collision"] = {
            "bounds": collision["bounds"],
            "vertices": len(collision["vertices"]),
            "polygons": len(collision["polygons"]),
            "surface_types": len(collision["surface_types"]),
            "water_boxes": len(collision["water_boxes"]),
            "camera_entries": len(collision["camera_data"]),
        }
    elif kind == "Array":
        reader = o2r.Reader(body, endian, path)
        array_type = reader.u32()
        if array_type != 25:
            record.update(status="unsupported", reason=f"array type {array_type} is not a vertex array")
            return
        record["vertices"] = o2r.parse_vertex_array(body, endian, path)["count"]
    elif kind == "Texture":
        texture = o2r.parse_texture_descriptor(body, endian, version, path)
        record["texture"] = {key: texture[key] for key in ("format", "width", "height",
                                                          "payload_size", "expected_packed_size_matches")}
        if texture["bits_per_pixel"] is None:
            limitations.add("unknown texture format")
        elif not texture["expected_packed_size_matches"]:
            limitations.add("texture payload differs from packed dimensions")
        if texture["format_code"] in (3, 4):
            limitations.add("palette use requires display-list execution")
    elif kind == "DisplayList":
        display_list = o2r.parse_display_list(body, endian, path)
        record["display_list"] = {
            "microcode": display_list["ucode"],
            "instructions": display_list["instruction_count"],
            "triangle_commands": display_list["triangle_count"] if display_list["ucode"] == 4 else None,
        }
        if display_list["ucode"] != 4:
            limitations.add("only F3DEX2 dependency and triangle semantics are checked")
        else:
            dependencies = set()
            unresolved = set()
            for instruction in display_list["instructions"]:
                opcode = instruction["opcode"]
                if opcode in (0x01, 0xDA, 0xDC, 0xDE, 0xFD) and instruction["w1"] != 0:
                    limitations.add("direct addresses require runtime segment bindings")
                if opcode in (0x35, 0x36, 0x42):
                    limitations.add("hashed branch, matrix or memory command is not lowered")
                if opcode not in HASH_REFERENCE_TYPES:
                    continue
                key = instruction["resource_hash"]
                candidates = hash_index.get(int(key, 16), [])
                expected = HASH_REFERENCE_TYPES[opcode]
                if len(candidates) == 1 and (expected is None or resources[candidates[0]]["type"] in expected):
                    dependencies.add(candidates[0])
                else:
                    unresolved.add((instruction["name"], key))
            record["dependencies"] = sorted(dependencies)
            if unresolved:
                record["unresolved_hashes"] = [{"command": command, "hash": key}
                                               for command, key in sorted(unresolved)]
                limitations.add("one or more hashed references could not be resolved")

    record["status"] = "partial" if limitations else "parsed"
    if limitations:
        record["limitations"] = sorted(limitations)


def dependency_coverage(root: str, resources: dict) -> dict:
    queued = [root]
    visited = set()
    type_counts, status_counts = Counter(), Counter()
    missing = set()
    while queued:
        path = queued.pop()
        if path in visited:
            continue
        visited.add(path)
        record = resources.get(path)
        if record is None:
            missing.add(path)
            continue
        type_counts[record["type"] or "untyped"] += 1
        status_counts[record["status"]] += 1
        queued.extend(record.get("dependencies", ()))
    return {
        "resource_counts": dict(sorted(type_counts.items())),
        "parser_status_counts": dict(sorted(status_counts.items())),
        "missing_paths": sorted(missing),
    }


def build_scene_index(resources: dict) -> tuple[list[dict], dict, dict]:
    headers = defaultdict(list)
    for path, record in resources.items():
        if record["type"] == "Scene":
            headers[PurePosixPath(path).parent].append(path)

    scenes, room_links, unresolved_room_links, cross_scene_exits = [], [], [], []
    unclassified_headers = []
    all_rooms, all_alternates = set(), set()
    for directory, candidates in sorted(headers.items()):
        preferred = str(directory / directory.name)
        conventional = str(directory / "Scene")
        root = preferred if preferred in candidates else conventional if conventional in candidates else None
        if root is None:
            unclassified_headers.extend(candidates)
            continue
        root_metadata = resources[root].get("scene", {})
        root_rooms = root_metadata.get("rooms", [])
        rooms = set(root_rooms)
        alternates = set()
        for path in candidates:
            metadata = resources[path].get("scene", {})
            active_rooms = metadata.get("rooms") or root_rooms
            rooms.update(active_rooms)
            alternates.update(name for name in metadata.get("alternate_headers", []) if name)
            for slot, transition in enumerate(metadata.get("transitions", [])):
                front, back = transition["front_room"], transition["back_room"]
                edge = {"scene": root, "header": path, "slot": slot, **transition}
                if (front < len(active_rooms) and back < len(active_rooms)
                        and all("scene" in resources.get(active_rooms[index], {})
                                for index in (front, back))):
                    room_links.append({**edge, "from_room": active_rooms[front], "to_room": active_rooms[back]})
                else:
                    unresolved_room_links.append({**edge, "reason": "room index or room header unavailable"})
            for slot, entrance_id in enumerate(metadata.get("exit_ids", [])):
                cross_scene_exits.append({"scene": root, "header": path, "slot": slot,
                                          "entrance_id": entrance_id,
                                          "reason": "global entrance mapping required"})
        all_rooms.update(rooms)
        all_alternates.update(alternates)
        unclassified_headers.extend(sorted(set(candidates) - rooms - alternates - {root}))
        scenes.append({
            "path": root,
            "variant": directory.parts[1] if directory.parts[:1] == ("scenes",)
                       and len(directory.parts) >= 3 else None,
            "rooms": root_rooms,
            "room_headers": sorted(rooms),
            "alternate_headers": sorted(alternates),
            "spawn_count": root_metadata.get("spawn_count"),
            "entrances": root_metadata.get("entrances", []),
            "coverage": dependency_coverage(root, resources),
        })
    graph = {"room_links": room_links, "unresolved_room_links": unresolved_room_links,
             "unresolved_cross_scene_exits": cross_scene_exits}
    counts = {"scenes": len(scenes),
              "room_headers": sum(resources.get(path, {}).get("type") == "Scene" for path in all_rooms),
              "alternate_headers": sum(resources.get(path, {}).get("type") == "Scene" for path in all_alternates),
              "room_links": len(room_links), "unresolved_room_links": len(unresolved_room_links),
              "unresolved_cross_scene_exits": len(cross_scene_exits),
              "unclassified_scene_headers": sorted(unclassified_headers)}
    return scenes, graph, counts


def inventory_archive(archive_path: Path, *, verify_crc: bool = False) -> dict:
    """Return parser coverage and transition evidence. Never extract archive files."""
    with ZipFile(archive_path, "r") as archive:
        index = archive_index(archive)
        if verify_crc:
            bad = archive.testzip()
            if bad is not None:
                raise o2r.ImportFailure(f"ZIP CRC validation failed for {bad}")
        resources = {}
        hash_index = defaultdict(list)
        for path, entry in sorted(index.items()):
            with archive.open(entry) as stream:
                header = o2r.read_resource_header(stream.read(o2r.OTR_HEADER_SIZE), path)
            resources[path] = {
                "path": path,
                "bytes": entry.file_size,
                "type": header.get("resource_type"),
                "version": header.get("version"),
                "endian": header.get("endian"),
                "header_status": header["status"],
                "status": "unsupported",
            }
            hash_index[o2r.crc64_resource_path(path)].append(path)

        for path, record in resources.items():
            if record["type"] not in PARSER_TYPES:
                record["reason"] = "no inventory parser for this resource type"
                continue
            try:
                body = archive.read(index[path])[o2r.OTR_HEADER_SIZE:]
                summarize_resource(record, body, hash_index, resources)
            except o2r.ImportFailure as exc:
                record.update(status="invalid", reason=str(exc))

    scenes, graph, counts = build_scene_index(resources)
    return {
        "format": "o2r-scene-inventory",
        "version": 1,
        "scope": "parser coverage only; no geometry, images, runtime conversion, or gameplay verification",
        "zip_crc_verified": verify_crc,
        "totals": {
            **counts,
            "files": len(resources),
            "parser_status_counts": dict(sorted(Counter(r["status"] for r in resources.values()).items())),
            "resource_counts": dict(sorted(Counter(r["type"] or "untyped" for r in resources.values()).items())),
        },
        "scenes": scenes,
        "resources": resources,
        "transition_graph": graph,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="local O2R archive")
    parser.add_argument("--verify-crc", action="store_true", help="read and validate all ZIP entries")
    parser.add_argument("--output", type=Path,
                        help="write the deterministic versioned scene/resource catalog here")
    args = parser.parse_args(argv)
    try:
        report = inventory_archive(args.archive, verify_crc=args.verify_crc)
    except (OSError, RuntimeError, BadZipFile, o2r.ImportFailure) as exc:
        parser.exit(1, f"inventory failed: {exc}\n")
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(serialized)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
