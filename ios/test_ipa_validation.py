"""Host-side unit checks for IPA parsing (fixtures are not installable apps)."""

import plistlib
from pathlib import Path
import stat
import struct
import tempfile
import unittest
import zipfile

from package_unsigned_ipa import package_app
from validate_ipa import validate


class IpaValidationTests(unittest.TestCase):
    def make_app(self, path: Path, cpu_type: int = 0x0100000C, platform: int = 2) -> Path:
        info = {
            "CFBundleExecutable": "sm64ios",
            "CFBundleIdentifier": "com.example.sm64",
            "CFBundleSupportedPlatforms": ["iPhoneOS"],
            "CFBundleShortVersionString": "1.0",
            "UIMainStoryboardFile": "Main",
            "UILaunchStoryboardName": "LaunchScreen",
            "UIDeviceFamily": [1],
            "UISupportedInterfaceOrientations": [
                "UIInterfaceOrientationLandscapeLeft",
                "UIInterfaceOrientationLandscapeRight",
            ],
        }
        # Minimal 64-bit little-endian Mach-O header sufficient to test the
        # architecture parser. This is not executable game code.
        binary = struct.pack("<IIIIIIII", 0xFEEDFACF, cpu_type, 0, 2, 1, 24, 0, 0)
        binary += struct.pack("<IIIIII", 0x32, 24, platform, 0x000D0200, 0x000D0200, 0)
        app = path / "SM64.app"
        app.mkdir()
        (app / "Info.plist").write_bytes(plistlib.dumps(info))
        executable = app / "sm64ios"
        executable.write_bytes(binary)
        executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
        (app / "Assets.car").write_bytes(b"asset catalog fixture")
        for storyboard in ("Main", "LaunchScreen"):
            resource = app / "Base.lproj" / f"{storyboard}.storyboardc"
            resource.mkdir(parents=True)
            (resource / "Info.plist").write_bytes(plistlib.dumps({"storyboard": storyboard}))
        return app

    def test_valid_arm64_ipa_structure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "valid.ipa"
            app = self.make_app(Path(temp))
            package_app(app, path)
            with zipfile.ZipFile(path) as ipa:
                mode = ipa.getinfo("Payload/SM64.app/sm64ios").external_attr >> 16
                self.assertTrue(mode & stat.S_IXUSR)
            validate(str(path), require_system_deps=False)

    def test_rejects_non_arm64_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "wrong-arch.ipa"
            app = self.make_app(Path(temp), cpu_type=0x01000007)
            package_app(app, path)
            with self.assertRaisesRegex(ValueError, "lacks an arm64 iPhoneOS"):
                validate(str(path), require_system_deps=False)

    def test_rejects_arm64_simulator_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "simulator.ipa"
            app = self.make_app(Path(temp), platform=7)
            package_app(app, path)
            with self.assertRaisesRegex(ValueError, "lacks an arm64 iPhoneOS"):
                validate(str(path), require_system_deps=False)


if __name__ == "__main__":
    unittest.main()
