"""Host-side unit checks for IPA parsing (fixtures are not installable apps)."""

import plistlib
from pathlib import Path
import subprocess
import stat
import struct
import tempfile
import unittest
import zipfile

from package_unsigned_ipa import package_app
from validate_ipa import validate


class IpaValidationTests(unittest.TestCase):
    def test_cachyos_local_commands_keep_apple_build_boundary_explicit(self) -> None:
        root = Path(__file__).resolve().parents[1]
        script = (root / "ios/build_cachyos_local.sh").read_text()
        app_build_script = (root / "build.sh").read_text()
        self.assertIn('python3 tools/hyrule_importer.py "$OOT_ARCHIVE"', script)
        self.assertIn("tests/run_linux_checks.sh", script)
        self.assertIn("TARGET_IOS=1 TARGET_OS_IOS=1 IOS_HYRULE_WORLD=1", script)
        self.assertIn("ios/package_unsigned_ipa.py", script)
        self.assertIn("ios/validate_ipa.py", script)
        self.assertNotIn("gh workflow run", script)
        self.assertIn("No IPA was built: this host lacks Xcode/xcrun", script)
        self.assertIn('IOS_CONNECTED_WORLD_TEST="$IOS_CONNECTED_WORLD_TEST_BUILD"',
                      app_build_script)

    def test_cachyos_local_package_command_accepts_real_app_bundle_structure(self) -> None:
        root = Path(__file__).resolve().parents[1]
        script = root / "ios/build_cachyos_local.sh"
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            app = self.make_app(temp_path, include_game_textures=True)
            ipa = temp_path / "sm64ios.ipa"
            result = subprocess.run(
                [str(script), "package", str(app), str(ipa)],
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(ipa.is_file())
            self.assertIn("valid iPhoneOS arm64 IPA", result.stdout)
            self.assertIn("Package structure is validated", result.stdout)

    def test_local_macos_builder_uses_pinned_local_inputs_and_refuses_linux(self) -> None:
        root = Path(__file__).resolve().parents[1]
        script = root / "ios/build_local_macos_ipa.sh"
        source = script.read_text()
        self.assertIn('"$SCRIPT_DIR/build_sdl2_ios.sh"', source)
        self.assertIn('HYRULE_O2R_PATH="$OOT_ARCHIVE"', source)
        self.assertIn('IOS_CONNECTED_WORLD_TEST="$CONNECTED_WORLD_MODE"', source)
        self.assertIn("CODE_SIGNING_ALLOWED=NO", source)
        self.assertIn("ios/package_unsigned_ipa.py", source)
        self.assertIn("ios/validate_ipa.py", source)
        self.assertNotIn("gh ", source)
        self.assertNotIn("curl ", source)
        if subprocess.run(["uname", "-s"], capture_output=True, text=True, check=True).stdout.strip() != "Darwin":
            result = subprocess.run([str(script)], text=True, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("requires macOS with Xcode", result.stderr)

    def test_ipa_workflow_preserves_private_asset_release_for_retries(self) -> None:
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/ios-metal-ipa.yml").read_text()
        self.assertIn("asset_bundle_release_tag", workflow)
        self.assertNotIn("gh release delete", workflow)
        self.assertIn("branches: [ios]", workflow)
        self.assertIn("contains(github.event.head_commit.message, '[build-ipa]')", workflow)
        self.assertIn("ASSET_BUNDLE_RELEASE_TAG=sm64-assets-20261031-04", workflow)
        self.assertIn("--require-game-textures", workflow)

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
