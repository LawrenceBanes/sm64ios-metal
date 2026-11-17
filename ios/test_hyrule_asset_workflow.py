#!/usr/bin/env python3
"""Guard the opt-in, private O2R input path for Hyrule-enabled iOS builds."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HyruleAssetWorkflowTests(unittest.TestCase):
    def test_hyrule_archive_is_opt_in_private_draft_and_checksum_validated(self):
        workflow = (ROOT / ".github/workflows/ios-metal-ipa.yml").read_text()
        script = (ROOT / "ios/build_ios_metal_ci.sh").read_text()
        self.assertIn("hyrule_o2r_release_tag:", workflow)
        self.assertIn("HYRULE_O2R_RELEASE_TAG: ${{ inputs.hyrule_o2r_release_tag }}", workflow)
        self.assertIn("hyrule_o2r_release_tag != '' && '1' || '0'", workflow)
        self.assertIn("^hyrule-o2r-[A-Za-z0-9._-]+$", script)
        self.assertIn('"$HYRULE_O2R_RELEASE_TAG" --repo "$SM64_ASSET_PRIVATE_REPOSITORY"', script)
        self.assertIn('d["isDraft"]', script)
        self.assertIn("oot.o2r.sha256", script)
        self.assertIn("Private O2R checksum mismatch", script)
        self.assertIn("bundle.testzip()", script)
        self.assertIn("scenes/shared/spot00_scene/spot00_scene", script)
        self.assertIn('export HYRULE_O2R_PATH="$private_dir/oot.o2r"', script)
        self.assertIn('IOS_HYRULE_WORLD="${IOS_HYRULE_WORLD:-0}"', script)
        self.assertIn('rm -rf -- "$private_dir"', script)

    def test_o2r_is_not_uploaded_as_an_actions_artifact_or_ipa_release_asset(self):
        workflow = (ROOT / ".github/workflows/ios-metal-ipa.yml").read_text()
        artifact = workflow.split("- name: Upload asset-free diagnostics only", 1)[1]
        self.assertIn("/safe/", artifact)
        self.assertNotIn("oot.o2r", artifact)
        self.assertNotIn(".ipa", artifact)
        self.assertNotIn("gh release create", workflow)


if __name__ == "__main__":
    unittest.main(verbosity=2)
