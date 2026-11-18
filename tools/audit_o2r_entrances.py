#!/usr/bin/env python3
"""Audit catalog slots against an optional local evidence map. No asset export."""

import argparse
from collections import Counter
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys

if __package__:
    from .scene_metadata import Layout, MetadataError, number, read_mapping
else:
    from scene_metadata import Layout, MetadataError, number, read_mapping


def scene_record(resources, path):
    record = resources.get(path, {})
    if record.get("type") != "Scene" or not isinstance(record.get("scene"), dict):
        raise MetadataError(f"scene header missing or undecoded: {path!r}")
    return record["scene"]


def read_layout(resources, header):
    scene = scene_record(resources, header)
    commands = scene["command_ids"]
    if scene["unsupported_at"] is not None or commands[-1:] != [20] or scene["trailing_bytes"]:
        raise MetadataError(f"incomplete scene header {header!r}")
    if any(commands.count(cmd) != 1 for cmd in (0, 4, 6)):
        raise MetadataError(f"header {header!r} must explicitly supply one player, room, and spawn list")
    layout = Layout.read(scene["rooms"], scene["spawn_count"], [
        {"player_entry": entry["spawn"], "room_index": entry["room"]} for entry in scene["entrances"]
    ])
    for room in layout.rooms:
        scene_record(resources, room)
    return layout


def signed_room(byte):
    if type(byte) is not int or not 0 <= byte <= 255:
        raise MetadataError("room side is not an unsigned byte")
    return byte if byte < 128 else byte - 256


def audit_inventory(catalog, mapping=None, catalog_digest=None, layer=0):
    number(layer, "layer offset", 0, 255)
    if catalog.get("format") != "o2r-scene-inventory" or catalog.get("version") != 1:
        raise MetadataError("expected o2r-scene-inventory version 1")
    resources = catalog["resources"]
    graph = catalog["transition_graph"]
    roots = {entry["path"]: entry for entry in catalog["scenes"]}
    if len(roots) != len(catalog["scenes"]):
        raise MetadataError("duplicate scene root in catalog")
    source, profile, special = read_mapping(mapping, catalog_digest) if mapping is not None else (None, {}, set())
    layout_cache, invalid_roots, slot_count = {}, {}, 0
    for root in sorted(roots):
        try:
            layout_cache[root] = read_layout(resources, root)
        except MetadataError as error:
            invalid_roots[root] = str(error)
        else:
            slot_count += len(layout_cache[root].entrances)

    # Validate every supplied row, even if no exit in this catalog references it.
    destinations, invalid_rows = {}, {}
    for key, entry in sorted(profile.items()):
        try:
            if entry.scene not in roots:
                raise MetadataError(f"destination scene missing: {entry.scene!r}")
            scene = scene_record(resources, entry.scene)
            if entry.header != entry.scene and entry.header not in scene["alternate_headers"]:
                raise MetadataError("selected header is not a scene-level alternate of its destination")
            if entry.header not in layout_cache:
                layout_cache[entry.header] = read_layout(resources, entry.header)
            destinations[key] = {**asdict(entry), **layout_cache[entry.header].destination(entry.local_slot)}
        except MetadataError as error:
            invalid_rows[key] = str(error)

    room_counts, room_issues = Counter(), []
    for record in graph["room_links"] + graph["unresolved_room_links"]:
        try:
            header = scene_record(resources, record["header"])
            # This reproduces the catalog's index scope for diagnostics. A
            # missing room list in an alternate still requires review before use.
            room_paths = header["rooms"] or scene_record(resources, record["scene"])["rooms"]
            links = Layout.read(room_paths, 0, [])
            front = signed_room(record["front_room"])
            back = signed_room(record["back_room"])
            status = links.room_link_status(front, back)
            for room in (front, back):
                if room != -1:
                    scene_record(resources, links.rooms[room])
        except MetadataError as error:
            status = "invalid"
            room_issues.append({**record, "reason": str(error)})
        room_counts[status] += 1

    rows, exit_counts = [], Counter()
    for exit_ in graph["unresolved_cross_scene_exits"]:
        index = exit_["entrance_id"]
        key = (index, layer)
        row = {key: exit_[key] for key in ("scene", "header", "slot", "entrance_id")}
        if index in special:
            status = "special_context_required"
        elif key in invalid_rows:
            status = "invalid_destination"
            row["reason"] = invalid_rows[key]
        elif key in destinations:
            status = "metadata_match"
            row["destination"] = destinations[key]
        else:
            status = "missing_mapping"
        row["status"] = status
        exit_counts[status] += 1
        rows.append(row)
    return {
        "format": "o2r-entrance-audit",
        "version": 1,
        "scope": "metadata only. Exit slots do not establish a trigger, usable spawn, or gameplay route.",
        "source": source,
        "catalog_sha256": catalog_digest,
        "layer": layer,
        "scene_roots": len(roots),
        "validated_default_layouts": len(roots) - len(invalid_roots),
        "validated_default_entrance_slots": slot_count,
        "invalid_default_layouts": invalid_roots,
        "room_link_status": dict(sorted(room_counts.items())),
        "invalid_room_records": room_issues,
        "distinct_exit_ids": len({row["entrance_id"] for row in rows}),
        "exit_status": dict(sorted(exit_counts.items())),
        "invalid_mapping_rows": [{"base": k[0], "layer": k[1], "reason": v}
                                 for k, v in sorted(invalid_rows.items())],
        "exit_slots": rows,
    }


def unique_json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise MetadataError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def read_json(path, max_bytes):
    with path.open("rb") as stream:
        data = stream.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise MetadataError(f"{path}: input exceeds {max_bytes} bytes")
    return json.loads(data, object_pairs_hook=unique_json_object), hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--mapping", type=Path, help="local scene-entrance-map JSON")
    parser.add_argument("--layer", type=int, metavar="0..255", default=0)
    parser.add_argument("--output", type=Path, help="write a private JSON report")
    args = parser.parse_args()
    try:
        catalog, digest = read_json(args.catalog, 128 * 1024 * 1024)
        mapping = read_json(args.mapping, 16 * 1024 * 1024)[0] if args.mapping else None
        result = audit_inventory(catalog, mapping, digest, args.layer)
        report = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(report, encoding="utf-8")
        else:
            sys.stdout.write(report)
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        parser.exit(2, f"entrance audit: {error}\n")
    if result["invalid_default_layouts"] or result["invalid_room_records"] or result["invalid_mapping_rows"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
