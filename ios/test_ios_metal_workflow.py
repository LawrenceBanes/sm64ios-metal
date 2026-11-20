#!/usr/bin/env python3
"""Guard the opt-in push build loop and private-vault credential scope."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/ios-metal-ipa.yml"


class IOSMetalWorkflowTests(unittest.TestCase):
    def test_main_builds_on_push_and_feature_branches_require_marker(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("branches: [main, ios, ci/ios-metal-m57-25c, 'mission-*', unified-build]", workflow)
        self.assertIn("workflow_dispatch:", workflow)
        self.assertGreaterEqual(workflow.count("contains(github.event.head_commit.message, '[build-ipa]')"), 3)
        self.assertNotIn("pull_request:", workflow)

    def test_main_push_runs_full_validated_ipa_without_weakening_checks(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn("ios-arm64-ipa:\n    needs: linux-checks", workflow)
        self.assertIn("github.ref == 'refs/heads/main' || contains(github.event.head_commit.message, '[build-ipa]')", workflow)
        self.assertIn("(github.ref == 'refs/heads/main' || inputs.scene_diagnostic", workflow)
        self.assertIn("IOS_CONNECTED_WORLD_TEST: '1'", workflow)

    def test_push_marker_can_enable_scene_diagnostic_without_manual_dispatch(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("contains(github.event.head_commit.message, '[scene-diagnostic]')", workflow)
        self.assertIn("inputs.scene_diagnostic", workflow)
        self.assertIn("IOS_O2R_SCENE_RUNTIME:", workflow)
        self.assertIn("IOS_SCENE_DIAGNOSTIC:", workflow)

    def test_optional_app_token_is_scoped_to_private_vault(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("vars.SM64_PRIVATE_BUILD_USE_GITHUB_APP == 'true'", workflow)
        self.assertIn("actions/create-github-app-token@v3", workflow)
        self.assertIn("repositories: sm64ios-private-builds", workflow)
        self.assertIn("permission-contents: write", workflow)
        self.assertIn("steps.private-vault-token.outputs.token || secrets.SM64_PRIVATE_BUILD_GH_TOKEN", workflow)
        self.assertIn("SM64_PRIVATE_BUILD_APP_ID", workflow)
        self.assertIn("SM64_PRIVATE_BUILD_APP_PRIVATE_KEY", workflow)

    def test_device_inputs_and_safe_artifact_handling_remain(self):
        workflow = WORKFLOW.read_text()
        for field in ("full_game:", "asset_bundle_release_tag:", "connected_world:",
                      "hat_movement:", "scene_diagnostic:", "hyrule_o2r_release_tag:"):
            self.assertIn(field, workflow)
        self.assertIn("Upload asset-free diagnostics only", workflow)
        self.assertNotIn("path: ${{ runner.temp }}/sm64-ios-ci/private/", workflow)


if __name__ == "__main__":
    unittest.main(verbosity=2)
