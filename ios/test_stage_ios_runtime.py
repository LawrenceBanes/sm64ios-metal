"""Host tests for runtime-only iOS app staging."""

from pathlib import Path
import tempfile
import unittest

from stage_ios_runtime import stage_runtime_payload


class StageIosRuntimeTests(unittest.TestCase):
    def make_build(self, root: Path) -> tuple[Path, Path]:
        build_dir = root / "build/us_pc"
        app_dir = root / "Products/SM64.app"
        build_dir.mkdir(parents=True)
        app_dir.mkdir(parents=True)
        (build_dir / "sm64ios").write_bytes(b"game executable")
        (build_dir / "sm64ios").chmod(0o755)
        intermediate = build_dir / "actors/group7_geo.o"
        intermediate.parent.mkdir(parents=True)
        intermediate.write_bytes(b"object file")
        dependency = build_dir / "src/engine/world_region.d"
        dependency.parent.mkdir(parents=True)
        dependency.write_text("dependency file")
        return build_dir, app_dir

    def test_external_data_stages_only_executable_and_runtime_resource_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build_dir, app_dir = self.make_build(root)
            resource_pack = build_dir / "res/base.zip"
            resource_pack.parent.mkdir(parents=True)
            resource_pack.write_bytes(b"runtime game resources")

            staged = stage_runtime_payload(build_dir, app_dir, external_data=True)

            self.assertEqual(staged, ["sm64ios", "res/base.zip"])
            self.assertEqual((app_dir / "sm64ios").read_bytes(), b"game executable")
            self.assertEqual((app_dir / "res/base.zip").read_bytes(), b"runtime game resources")
            self.assertTrue((app_dir / "sm64ios").stat().st_mode & 0o111)
            self.assertFalse((app_dir / "actors/group7_geo.o").exists())
            self.assertFalse((app_dir / "src/engine/world_region.d").exists())

    def test_embedded_data_stages_only_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            build_dir, app_dir = self.make_build(Path(temp))

            staged = stage_runtime_payload(build_dir, app_dir, external_data=False)

            self.assertEqual(staged, ["sm64ios"])
            self.assertEqual([p.relative_to(app_dir).as_posix() for p in app_dir.rglob("*") if p.is_file()], ["sm64ios"])

    def test_external_data_requires_the_runtime_resource_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            build_dir, app_dir = self.make_build(Path(temp))
            with self.assertRaisesRegex(ValueError, "external game resource pack"):
                stage_runtime_payload(build_dir, app_dir, external_data=True)


if __name__ == "__main__":
    unittest.main()
