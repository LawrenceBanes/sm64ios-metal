"""Host-side unit checks for IPA parsing (fixtures are not installable apps)."""

import plistlib
from pathlib import Path
import stat
import struct
import tempfile
import unittest
import zipfile

from package_unsigned_ipa import package_app
from validate_ipa import LOCAL_SCENE_STRINGS, SHITBOX_IDENTITY, STOCK_AUDIO, validate


class IpaValidationTests(unittest.TestCase):
    def test_ipa_workflow_requires_private_assets_and_never_publishes_game_payload(self) -> None:
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/ios-metal-ipa.yml").read_text()
        script = (Path(__file__).resolve().parent / "build_ios_metal_ci.sh").read_text()
        build_script = (Path(__file__).resolve().parents[1] / "build.sh").read_text()
        self.assertIn("asset_bundle_release_tag", workflow)
        self.assertIn("SM64_ASSET_PRIVATE_REPOSITORY", workflow)
        self.assertIn("SM64_PRIVATE_BUILD_GH_TOKEN", workflow)
        self.assertIn("github.repository == 'LawrenceBanes/sm64ios-metal'", workflow)
        self.assertIn("inputs.full_game ||", workflow)
        self.assertNotIn("gh release create", workflow)
        self.assertNotIn("Upload unsigned IPA", workflow)
        self.assertIn("stage_ios_runtime.py", build_script)
        self.assertNotIn('cp -R "$GAMEDIR_CP_SRC"', build_script)
        self.assertNotIn('"ios/" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"', build_script)
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

    def test_local_scene_build_checks_loader_audio_and_excludes_external_worlds(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = self.make_app(root)
            executable = app / "sm64ios"
            original_binary = executable.read_bytes()
            res = app / "res"
            res.mkdir()
            ipa = root / "local-scene.ipa"

            def pack_resources(*, omit_audio=False, external=None):
                with zipfile.ZipFile(res / "base.zip", "w") as pack:
                    for tag, ident in ((b"WSC2", 4098), (b"WSC3", 4099)):
                        pack.writestr(f"scene-diagnostic/scenes/{ident}.wsc", struct.pack("<4sI", tag, ident) + bytes(8))
                    pack.writestr("scene-diagnostic/scenes/4294967294.wsc", b"WSC2")
                    for texture in ("67f-ci4", "67f-ci8", "wsc3-grid", "wsc3-wave"):
                        png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I4sIIBBBBB", 13, b"IHDR", 16, 16, 8, 6, 0, 0, 0)
                        pack.writestr(f"scene-diagnostic/gfx/scenes/{texture}.rgba16.png", png)
                    for resource in STOCK_AUDIO[omit_audio:]:
                        pack.writestr(resource, b"stock audio fixture")
                    if external:
                        pack.writestr(external, b"unexpected external resource")
                package_app(app, ipa)

            pack_resources()
            with self.assertRaisesRegex(ValueError, "missing the Documents WSC5 Hyrule loader"):
                validate(str(ipa), False, require_local_scene_loader=True)
            executable.write_bytes(original_binary + b"\0Load Local WSC3 Scene\0Leave Local WSC3 Scene")
            pack_resources()
            with self.assertRaisesRegex(ValueError, "missing the Documents WSC5 Hyrule loader"):
                validate(str(ipa), False, require_local_scene_loader=True)
            executable.write_bytes(original_binary + b"\0".join(LOCAL_SCENE_STRINGS))
            pack_resources()
            validate(str(ipa), False, require_local_scene_loader=True)
            pack_resources(omit_audio=True)
            with self.assertRaisesRegex(ValueError, "missing or empty stock audio"):
                validate(str(ipa), False, require_local_scene_loader=True)
            for external in ("scenes/4100.wsc", "workspace/resource.o2r", "gfx/scenes/imported.rgba16.png"):
                with self.subTest(external=external):
                    pack_resources(external=external)
                    with self.assertRaisesRegex(ValueError, "external world asset"):
                        validate(str(ipa), False, require_local_scene_loader=True)

    def test_local_scene_loader_validator_requires_the_wsc5_hyrule_actions(self):
        self.assertEqual(LOCAL_SCENE_STRINGS, (
            b"Load Hyrule Field (WSC5)", b"Leave Hyrule Field", b"scenes/4100.wsc", b"oot-assets/runtime-index.tsv"
        ))

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

    def test_validates_all_packaged_identity_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            app = self.make_app(directory)
            plist = app / "Info.plist"
            info = {**plistlib.loads(plist.read_bytes()), **SHITBOX_IDENTITY}
            plist.write_bytes(plistlib.dumps(info))
            branded = directory / "branded.ipa"
            package_app(app, branded)
            validate(str(branded), require_system_deps=False, require_shitbox_identity=True)

            for key in SHITBOX_IDENTITY:
                with self.subTest(key=key):
                    plist.write_bytes(plistlib.dumps({**info, key: "outdated"}))
                    invalid = directory / f"{key}.ipa"
                    package_app(app, invalid)
                    with self.assertRaisesRegex(ValueError, f"Info.plist {key}"):
                        validate(str(invalid), require_system_deps=False, require_shitbox_identity=True)

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

    def test_rejects_build_intermediates_in_app_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = self.make_app(root)
            for relative in (
                "bin/wdw_skybox.o",
                "actors/group7_geo.o",
                "levels/castle_grounds/geo.o",
                "src/engine/world_region.o",
                "src/engine/world_region.d",
                "lib/libtemporary.a",
            ):
                path = app / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"build intermediate fixture")
            ipa_path = root / "intermediates.ipa"
            package_app(app, ipa_path)
            with self.assertRaisesRegex(ValueError, "app bundle contains build intermediate"):
                validate(str(ipa_path), require_system_deps=False)

    def test_rejects_build_intermediates_in_external_resource_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = self.make_app(root, include_game_textures=True)
            pack_path = app / "res/base.zip"
            with zipfile.ZipFile(pack_path, "a") as pack:
                pack.writestr("src/engine/world_region.o", b"build intermediate fixture")
            ipa_path = root / "resource-intermediate.ipa"
            package_app(app, ipa_path)
            with self.assertRaisesRegex(ValueError, "app resource pack contains build intermediate"):
                validate(str(ipa_path), require_system_deps=False)


if __name__ == "__main__":
    unittest.main()
