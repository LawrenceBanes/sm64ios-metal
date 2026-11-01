#!/usr/bin/env python3
"""Guard the opt-in, private O2R input path for Hyrule-enabled iOS builds."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HyruleAssetWorkflowTests(unittest.TestCase):
    def test_hyrule_archive_is_opt_in_private_draft_and_checksum_validated(self):
        workflow = (ROOT / ".github/workflows/ios-metal-ipa.yml").read_text()
        self.assertIn("hyrule_o2r_release_tag:", workflow)
        self.assertIn("HYRULE_O2R_RELEASE_TAG: ${{ inputs.hyrule_o2r_release_tag }}", workflow)
        self.assertIn("^hyrule-o2r-[A-Za-z0-9._-]+$", workflow)
        self.assertIn('"Temporary private Ocarina archive for Hyrule-enabled iOS IPA build."', workflow)
        self.assertIn("gh release download \"$HYRULE_O2R_RELEASE_TAG\"", workflow)
        self.assertIn("oot.o2r.sha256", workflow)
        self.assertIn("HYRULE_O2R_PATH=$RUNNER_TEMP/oot.o2r", workflow)
        self.assertIn('IOS_HYRULE_WORLD=1', workflow)
        self.assertIn('"scenes/shared/spot00_scene/spot00_scene" not in names', workflow)
        self.assertIn('rm -f "$RUNNER_TEMP/oot.o2r" "$RUNNER_TEMP/oot.o2r.sha256"', workflow)

    def test_o2r_is_not_uploaded_as_an_actions_artifact_or_ipa_release_asset(self):
        workflow = (ROOT / ".github/workflows/ios-metal-ipa.yml").read_text()
        artifact = re.search(r"- name: Upload private IPA and build logs\n(?P<body>.*?)(?=\n      - name:)", workflow, re.S)
        self.assertIsNotNone(artifact)
        self.assertNotIn("oot.o2r", artifact.group("body"))
        publish = re.search(r"- name: Publish this build as a new private prerelease\n(?P<body>.*?)(?=\n      - name:)", workflow, re.S)
        self.assertIsNotNone(publish)
        self.assertNotIn("oot.o2r", publish.group("body"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
