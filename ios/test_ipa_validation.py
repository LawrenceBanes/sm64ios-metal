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
    def test_ipa_workflow_preserves_private_asset_release_for_retries(self) -> None:
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/ios-metal-ipa.yml").read_text()
        self.assertIn("asset_bundle_release_tag", workflow)
        self.assertNotIn("gh release delete", workflow)
        self.assertIn("branches: [ios]", workflow)
        self.assertIn("contains(github.event.head_commit.message, '[build-ipa]')", workflow)
        self.assertIn("ASSET_BUNDLE_RELEASE_TAG=sm64-assets-20261031-04", workflow)

    def make_app(self, path: Path, cpu_type: int = 0x0100000C, platform: int = 2) -> Path:
        info = {
            "CFBundleExecutable": "sm64ios",
            "CFBundleIdentifier": "com.example.sm64",
            "CFBundleSupportedPlatforms": ["iPhoneOS"],
            "CFBundleShortVersionString": "1.0",
            "UIFileSharingEnabled": True,
            "LSSupportsOpeningDocumentsInPlace": True,
            "UIMainStoryboardFile": "Main",
            "UILaunchStoryboardName": "LaunchScreen",
            "UIDeviceFamily": [1],
            "UISupportedInterfaceOrientations": [
                "UIInterfaceOrientationLandscapeLeft",
                "UIInterfaceOrientationLandscapeRight",
            ],
            "UIApplicationSceneManifest": {
                "UIApplicationSupportsMultipleScenes": False,
                "UISceneConfigurations": {
                    "UIWindowSceneSessionRoleApplication": [
                        {
                            "UISceneConfigurationName": "Default Configuration",
                            "UISceneDelegateClassName": "SM64SceneDelegate",
                        }
                    ]
                },
            },
        }
        # Minimal 64-bit little-endian Mach-O header sufficient to test the
        # architecture parser. This is not executable game code.
        binary = struct.pack("<IIIIIIII", 0xFEEDFACF, cpu_type, 0, 2, 1, 24, 0, 0)
        binary += struct.pack("<IIIIII", 0x32, 24, platform, 0x000D0200, 0x000D0200, 0)
        binary += b"SM64SceneDelegate\0"
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

    def test_rejects_ipa_without_scene_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = self.make_app(root)
            info_path = app / "Info.plist"
            info = plistlib.loads(info_path.read_bytes())
            del info["UIApplicationSceneManifest"]
            info_path.write_bytes(plistlib.dumps(info))
            path = root / "missing-scene.ipa"
            package_app(app, path)
            with self.assertRaisesRegex(ValueError, "missing its UIKit application scene"):
                validate(str(path), require_system_deps=False)


if __name__ == "__main__":
    unittest.main()
