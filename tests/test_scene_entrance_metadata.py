"""Validate entrance interpretation using only authored scene metadata."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.audit_o2r_entrances import audit_inventory, read_json, signed_room
from tools.scene_metadata import Layout, MetadataError, read_mapping

DIGEST = "1" * 64


def header(*, rooms=None, entries=None, spawns=0, alternates=None):
    return {
        "type": "Scene",
        "scene": {
            "command_ids": [0, 4, 6, 20], "unsupported_at": None, "trailing_bytes": 0,
            "rooms": rooms or [], "spawn_count": spawns, "entrances": entries or [],
            "alternate_headers": alternates or [],
        },
    }


def catalog():
    return {
        "format": "o2r-scene-inventory", "version": 1, "scenes": [{"path": "hall"}, {"path": "garden"}],
        "resources": {
            "hall": header(rooms=["library"], entries=[{"spawn": 0, "room": 0}], spawns=1),
            "garden": header(rooms=["patio", "shed"], entries=[{"spawn": 2, "room": 1}],
                             spawns=3, alternates=["garden_evening"]),
            "garden_evening": header(rooms=["shed", "patio"],
                                     entries=[{"spawn": 1, "room": 1}], spawns=2),
            **{room: header() for room in ("library", "patio", "shed")},
        },
        "transition_graph": {
            "room_links": [{"scene": "garden", "header": "garden", "slot": 0,
                            "front_room": 0, "back_room": 1}],
            "unresolved_room_links": [{"scene": "garden", "header": "garden", "slot": 1,
                                       "front_room": 255, "back_room": 0}],
            "unresolved_cross_scene_exits": [
                {"scene": "hall", "header": "hall", "slot": slot, "entrance_id": entrance}
                for slot, entrance in enumerate((0, 19, 0, 99, 65535))
            ],
        },
    }


def mapping():
    row = {"base": 0, "layer": 0, "table_row": 0, "scene_id": 8,
           "scene": "garden", "header": "garden", "local_slot": 0}
    return {"format": "scene-entrance-map", "version": 1, "source": "authored fixture revision 1",
            "catalog_sha256": DIGEST, "special": [65535],
            "rows": [row, {**row, "layer": 1, "table_row": 1, "header": "garden_evening"}]}


class EntranceValidationTests(unittest.TestCase):
    def test_independent_indices_and_zero_is_valid(self):
        layout = Layout.read(["west", "east"], 3, [{"player_entry": 2, "room_index": 1}])
        self.assertEqual(layout.destination(0),
                         {"local_slot": 0, "player_entry": 2, "room_index": 1, "room": "east"})
        report = audit_inventory(catalog(), mapping(), DIGEST)
        self.assertEqual(report["exit_status"],
                         {"metadata_match": 2, "missing_mapping": 2, "special_context_required": 1})
        destination = report["exit_slots"][0]["destination"]
        self.assertEqual((destination["scene_id"], destination["local_slot"],
                          destination["player_entry"], destination["room"]), (8, 0, 2, "shed"))
        self.assertEqual([row["slot"] for row in report["exit_slots"] if row["status"] == "metadata_match"], [0, 2])

    def test_explicit_layer_and_missing_layer(self):
        selected = audit_inventory(catalog(), mapping(), DIGEST, layer=1)["exit_slots"][0]["destination"]
        self.assertEqual((selected["table_row"], selected["room"], selected["player_entry"]), (1, "patio", 1))
        missing = audit_inventory(catalog(), mapping(), DIGEST, layer=2)["exit_slots"][0]
        self.assertEqual(missing["status"], "missing_mapping")
        self.assertNotIn("destination", missing)

    def test_room_sentinels_and_invalid_indices(self):
        report = audit_inventory(catalog())
        self.assertEqual(report["room_link_status"], {"one_sided": 1, "two_sided": 1})
        bad = catalog()
        bad["transition_graph"]["unresolved_room_links"][0]["front_room"] = 254
        self.assertEqual(audit_inventory(bad)["room_link_status"], {"invalid": 1, "two_sided": 1})
        self.assertEqual(signed_room(255), -1)
        self.assertEqual(Layout.read(["room"], 0, []).room_link_status(0, 0), "same_room")

    def test_bad_destination_rows_are_reported_even_if_unused(self):
        for key, value in (("header", "patio"), ("header", "hall"),
                           ("scene", "unknown"), ("local_slot", 1)):
            with self.subTest(key=key, value=value):
                test_map = mapping()
                test_map["rows"].pop()
                test_map["rows"][0].update({key: value, "base": 333, "table_row": 333})
                report = audit_inventory(catalog(), test_map, DIGEST)
                self.assertEqual(len(report["invalid_mapping_rows"]), 1)
                self.assertEqual(report["exit_status"].get("metadata_match", 0), 0)

    def test_partial_headers_have_no_implicit_inheritance(self):
        test_catalog = catalog()
        alt = test_catalog["resources"]["garden_evening"]["scene"]
        alt["command_ids"] = [0, 6, 20]
        report = audit_inventory(test_catalog, mapping(), DIGEST, layer=1)
        self.assertEqual(report["exit_status"]["invalid_destination"], 2)
        self.assertEqual(report["exit_slots"][0]["status"], "invalid_destination")

    def test_collision_with_specials_and_conflicts_are_rejected(self):
        cases = [
            ("special", [0]),
            ("rows", [mapping()["rows"][0]] * 2),
            ("rows", [{**mapping()["rows"][0], "table_row": 2}]),
            ("rows", [{**mapping()["rows"][0], "base": True}]),
            ("rows", [mapping()["rows"][0], {**mapping()["rows"][1], "scene": "hall"}]),
        ]
        for key, value in cases:
            with self.subTest(key=key, value=value), self.assertRaises(MetadataError):
                read_mapping({**mapping(), key: value}, DIGEST)

    def test_context_binding_and_invalid_local_entries(self):
        with self.assertRaisesRegex(MetadataError, "catalog SHA-256"):
            read_mapping(mapping(), "2" * 64)
        for spawn, room in ((3, 0), (1, 2), (-1, 0)):
            test_catalog = copy.deepcopy(catalog())
            test_catalog["resources"]["garden"]["scene"]["entrances"][0] = {"spawn": spawn, "room": room}
            self.assertIn("garden", audit_inventory(test_catalog)["invalid_default_layouts"])

    def test_json_reader_rejects_duplicate_keys_and_oversized_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "input.json"
            path.write_text('{"id":1,"id":2}')
            with self.assertRaisesRegex(MetadataError, "duplicate JSON key"):
                read_json(path, 100)
            with self.assertRaisesRegex(MetadataError, "input exceeds"):
                read_json(path, 4)
            path.write_text(json.dumps({"id": 1}))
            self.assertEqual(read_json(path, 100)[0], {"id": 1})


if __name__ == "__main__":
    unittest.main()
