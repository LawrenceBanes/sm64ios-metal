import plistlib
import tempfile
import unittest
from pathlib import Path

from validate_app_identity import validate


class AppIdentityTests(unittest.TestCase):
    def write_info(self, values):
        temp = tempfile.NamedTemporaryFile(delete=False)
        with temp:
            plistlib.dump(values, temp)
        self.addCleanup(Path(temp.name).unlink, missing_ok=True)
        return Path(temp.name)

    def valid_info(self):
        return {
            "CFBundleIdentifier": "com.larry.shitboxdev",
            "CFBundleDisplayName": "SHITBOX LABZ INC.",
            "CFBundleShortVersionString": "beta.069420INITIAL",
            "CFBundleVersion": "beta.069420INITIAL",
        }

    def test_accepts_required_branded_identity(self):
        validate(self.write_info(self.valid_info()))

    def test_rejects_wrong_bundle_identifier(self):
        info = self.valid_info()
        info["CFBundleIdentifier"] = "com.christiankosman.sm64ex"
        with self.assertRaisesRegex(ValueError, "CFBundleIdentifier"):
            validate(self.write_info(info))

    def test_rejects_wrong_display_name_or_version(self):
        info = self.valid_info()
        info["CFBundleDisplayName"] = "Super Mario 64"
        info["CFBundleVersion"] = "1"
        with self.assertRaisesRegex(ValueError, "CFBundleDisplayName.*CFBundleVersion"):
            validate(self.write_info(info))


if __name__ == "__main__":
    unittest.main()
