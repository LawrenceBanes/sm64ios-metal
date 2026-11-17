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
    def test_ipa_workflow_requires_private_assets_and_never_publishes_game_payload(self) -> None:
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/ios-metal-ipa.yml").read_text()
        script = (Path(__file__).resolve().parent / "build_ios_metal_ci.sh").read_text()
        self.assertIn("asset_bundle_release_tag", workflow)
        self.assertIn("SM64_ASSET_PRIVATE_REPOSITORY", workflow)
        self.assertIn("SM64_ASSET_GH_TOKEN", workflow)
        self.assertIn("github.repository == 'LawrenceBanes/sm64ios-metal'", workflow)
        self.assertIn("if: ${{ inputs.full_game }}", workflow)
        self.assertNotIn("gh release create", workflow)
        self.assertNotIn("Upload unsigned IPA", workflow)
        self.assertIn("Asset repository is not private", script)
        self.assertIn("Public source repository cannot be the asset repository", script)
        self.assertIn("--require-game-textures", script)

    def make_app(self, path: Path, cpu_type: int = 0x0100000C, platform: int = 2,
                 include_game_textures: bool = False) -> Path:
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
        if include_game_textures:
            resource_dir = app / "res"
            resource_dir.mkdir()
            with zipfile.ZipFile(resource_dir / "base.zip", "w") as resource_pack:
                for name in (
                    "gfx/actors/goomba/goomba_face.rgba16.png",
                    "gfx/levels/bob/0.rgba16.png",
                    "gfx/levels/castle_inside/1.rgba16.png",
                ):
                    resource_pack.writestr(name, b"\x89PNG\r\n\x1a\nfixture")
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

    def test_requires_original_goomba_castle_and_battlefield_textures(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "textures.ipa"
            app = self.make_app(Path(temp), include_game_textures=True)
            package_app(app, path)
            validate(str(path), require_system_deps=False, require_game_textures=True)

    def test_rejects_ipa_without_original_texture_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "no-textures.ipa"
            app = self.make_app(Path(temp))
            package_app(app, path)
            with self.assertRaisesRegex(ValueError, "missing res/base.zip"):
                validate(str(path), require_system_deps=False, require_game_textures=True)

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
