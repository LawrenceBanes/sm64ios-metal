"""Validate scene metadata and explicit entrance rows, independent of payload format."""

from dataclasses import dataclass
import re


class MetadataError(ValueError):
    pass


def number(value, label, low=0, high=65535):
    if type(value) is not int or not low <= value <= high:
        raise MetadataError(f"{label} must be an integer in [{low}, {high}]")
    return value


def label(value, field):
    if not isinstance(value, str) or not value or len(value) > 1024:
        raise MetadataError(f"{field} must be a nonempty string of at most 1024 characters")
    return value


@dataclass(frozen=True)
class Layout:
    """Room order and player entry indices from one selected scene header."""

    rooms: tuple[str, ...]
    spawns: int
    entrances: tuple[tuple[int, int], ...]

    @classmethod
    def read(cls, rooms, spawn_count, entrances):
        if not isinstance(rooms, list) or not isinstance(entrances, list):
            raise MetadataError("rooms and entrances must be arrays")
        number(len(rooms), "room count", 0, 4096)
        number(len(entrances), "entrance count", 0, 4096)
        room_keys = tuple(label(room, "room key") for room in rooms)
        if len(room_keys) != len(set(room_keys)):
            raise MetadataError("duplicate room key")
        spawns = number(spawn_count, "player entry count", 0, 4096)
        slots = []
        for index, entrance in enumerate(entrances):
            if not isinstance(entrance, dict):
                raise MetadataError(f"entrance slot {index} must be an object")
            player = number(entrance.get("player_entry"), f"slot {index} player entry", 0, spawns - 1)
            room = number(entrance.get("room_index"), f"slot {index} room index", 0, len(rooms) - 1)
            slots.append((player, room))
        return cls(room_keys, spawns, tuple(slots))

    def destination(self, slot):
        slot = number(slot, "local entrance slot", 0, len(self.entrances) - 1)
        player, room = self.entrances[slot]
        return {"local_slot": slot, "player_entry": player, "room_index": room, "room": self.rooms[room]}

    def room_link_status(self, front, back):
        """A missing side is meaningful metadata. It is not a two-room edge."""
        for room in (front, back):
            number(room, "transition side", -1, len(self.rooms) - 1)
        if front == -1 and back == -1:
            return "no_rooms"
        if front == -1 or back == -1:
            return "one_sided"
        if front == back:
            return "same_room"
        return "two_sided"


@dataclass(frozen=True)
class EntranceRow:
    base: int
    layer: int
    table_row: int
    scene_id: int
    scene: str
    header: str
    local_slot: int


def read_mapping(doc, catalog_digest):
    """Read evidence rows for one catalog. Each layer stores its exact table row."""
    if (not isinstance(doc, dict) or doc.get("format") != "scene-entrance-map"
            or type(doc.get("version")) is not int or doc["version"] != 1):
        raise MetadataError("expected scene-entrance-map version 1")
    source = label(doc.get("source"), "mapping source and revision")
    digest = doc.get("catalog_sha256")
    if not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest) or digest != catalog_digest:
        raise MetadataError("entrance map is not bound to this exact catalog SHA-256")
    rows = doc.get("rows")
    specials = doc.get("special", [])
    if not isinstance(rows, list) or not isinstance(specials, list):
        raise MetadataError("rows and special must be arrays")
    number(len(rows), "row count", 0, 65536)
    number(len(specials), "special count", 0, 65536)
    special = set()
    for index in specials:
        index = number(index, "special index")
        if index in special:
            raise MetadataError(f"duplicate special index {index:#x}")
        special.add(index)

    profile = {}
    scene_names, scene_ids, table_targets = {}, {}, {}
    for row in rows:
        if not isinstance(row, dict):
            raise MetadataError("each row must be an object")
        entry = EntranceRow(
            number(row.get("base"), "base entrance index"),
            number(row.get("layer"), "layer offset", 0, 255),
            number(row.get("table_row"), "table row index"),
            number(row.get("scene_id"), "scene table ID"),
            label(row.get("scene"), "scene key"),
            label(row.get("header"), "selected layout key"),
            number(row.get("local_slot"), "local entrance slot"),
        )
        if entry.table_row != entry.base + entry.layer:
            raise MetadataError("table row must equal base entrance plus explicit layer offset")
        if entry.base in special or entry.table_row in special:
            raise MetadataError("special entrance overlaps a mapped table row")
        key = (entry.base, entry.layer)
        if key in profile:
            raise MetadataError(f"duplicate row for base {entry.base:#x}, layer {entry.layer}")
        if scene_names.setdefault(entry.scene_id, entry.scene) != entry.scene:
            raise MetadataError(f"scene ID {entry.scene_id:#x} has conflicting scene keys")
        if scene_ids.setdefault(entry.scene, entry.scene_id) != entry.scene_id:
            raise MetadataError(f"scene {entry.scene!r} has conflicting numeric IDs")
        target = (entry.scene_id, entry.local_slot)
        if table_targets.setdefault(entry.table_row, target) != target:
            raise MetadataError(f"table row {entry.table_row:#x} has conflicting destinations")
        profile[key] = entry
    return source, profile, special
