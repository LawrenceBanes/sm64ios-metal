#!/usr/bin/env python3
"""Source-level checks for Hyrule gate placement and safe unavailable behavior."""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class HyruleGateIntegrationTests(unittest.TestCase):
    def test_gate_anchor_comes_from_supported_imported_scene_start(self):
        importer = (ROOT / "tools/hyrule_importer.py").read_text()
        self.assertIn('gate_entry = start_entries[4] if len(start_entries) > 4 else None', importer)
        self.assertIn('"gate_position": gate_position', importer)
        self.assertIn('("HYRULE_FIELD_GATE", "gate_position")', importer)
        self.assertIn('"Hyrule Field gate-area start-position entry 4 is missing"', importer)
        self.assertIn('"Hyrule Field gate entrance 4 has no nearby collision floor"', importer)

    def test_no_gate_destination_is_registered_or_declared(self):
        regions = json.loads((ROOT / "ios/hyrule_regions.json").read_text())
        kokiri = next(region for region in regions["regions"] if region["id"] == "kokiri_forest")
        self.assertEqual(kokiri["status"], "planned")
        level_defines = (ROOT / "levels/level_defines.h").read_text()
        self.assertNotRegex(level_defines, r'LEVEL_KOKIRI_FOREST')
        script = (ROOT / "levels/hyrule_field/script.c").read_text()
        self.assertRegex(script, r'WARP_NODE\([\s\S]*?LEVEL_CASTLE_GROUNDS[\s\S]*?0x09')
        self.assertIn('0x28010000, bhvFadingWarp', script)
        self.assertNotIn("KOKIRI_FOREST", script)
        self.assertNotRegex(script, r"(?i)gate.{0,80}(destination|warp)")

    def test_proximity_feedback_is_debug_only_and_does_not_issue_unknown_warp(self):
        mario = (ROOT / "src/game/mario.c").read_text()
        guard = "#if defined(IOS_BUILD) && defined(IOS_HYRULE_WORLD) && defined(IOS_HYRULE_GATE_DIAGNOSTICS)"
        self.assertIn(guard, mario)
        self.assertIn("hyrule_gate_contains(m->pos[0], m->pos[1], m->pos[2]", mario)
        self.assertIn("if (m->floor == NULL)", mario)
        self.assertIn("hyrule_gate_response(\n            true, gateDestinationConfigured, LEVEL_NONE, 0, 0)", mario)
        self.assertIn('"Gate destination is not configured"', mario)
        activation = mario.split("static void mario_ios_update_hyrule_gate_debug", 1)[1].split("#endif", 1)[0]
        self.assertIn("warpIssued=0", activation)
        self.assertNotRegex(activation, r"Kokiri|initiate_warp|level_trigger_warp|initiate_delayed_warp")

    def test_gate_probe_runs_after_geometry_refresh_and_before_action_processing(self):
        mario = (ROOT / "src/game/mario.c").read_text()
        execute = mario.split("s32 execute_mario_action(", 1)[1]
        self.assertLess(execute.index("update_mario_inputs(gMarioState);"),
                        execute.index("mario_ios_update_hyrule_gate_debug(gMarioState);"))
        self.assertLess(execute.index("mario_ios_update_hyrule_gate_debug(gMarioState);"),
                        execute.index("mario_handle_special_floors(gMarioState);"))

    def test_existing_hyrule_return_warp_remains_the_only_documented_destination(self):
        script = (ROOT / "levels/hyrule_field/script.c").read_text()
        warp_nodes = re.findall(r"WARP_NODE\([^\n]+", script)
        self.assertEqual(len(warp_nodes), 1)
        self.assertIn("LEVEL_CASTLE_GROUNDS", warp_nodes[0])
        self.assertIn("LEVEL_CASTLE_GROUNDS", warp_nodes[0])
        self.assertIn("0x09", script.split(warp_nodes[0], 1)[1].split("),", 1)[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
