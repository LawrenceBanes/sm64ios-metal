#!/usr/bin/env python3
"""Verify the gate diagnostic reaches the real C preprocessor via Make."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GATE_FUNCTION = "mario_ios_update_hyrule_gate_debug"


def make_version_flags(hyrule: int, diagnostics: int) -> list[str]:
    result = subprocess.run(
        [
            "make", "--no-print-directory", "-pn", "clean", "NOEXTRACT=1",
            "TARGET_IOS=1", "TARGET_OS_IOS=1", "RENDER_API=METAL",
            "DEBUG=1", f"IOS_HYRULE_WORLD={hyrule}",
            f"IOS_HYRULE_GATE_DIAGNOSTICS={diagnostics}",
        ],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    match = re.search(r"^VERSION_CFLAGS\s*:=\s*(.*)$", result.stdout, re.MULTILINE)
    if not match:
        raise AssertionError("Make did not emit expanded VERSION_CFLAGS")
    return shlex.split(match.group(1))


def preprocess_mario(flags: list[str], temp_root: Path) -> str:
    generated = temp_root / "build/hyrule_field_spawn.h"
    generated.parent.mkdir(parents=True, exist_ok=True)
    generated.write_text(
        "#define HYRULE_FIELD_GATE_X 0\n"
        "#define HYRULE_FIELD_GATE_Y 0\n"
        "#define HYRULE_FIELD_GATE_Z 0\n",
        encoding="ascii",
    )
    common = shlex.split(os.environ.get("HYRULE_TEST_CPPFLAGS", ""))
    common.extend([
        "-E", "-P", "-std=gnu99", "-D_LANGUAGE_C", "-DF3DEX_GBI_2",
        "-DWAPI_SDL2=1", "-DAAPI_SDL2=1", "-DCAPI_SDL2=1", "-DHAVE_SDL2=1",
        f"-I{temp_root}", f"-I{ROOT / 'include'}", f"-I{ROOT / 'src'}", f"-I{ROOT}",
    ])
    result = subprocess.run(
        [os.environ.get("CC", "clang"), *common, *flags, str(ROOT / "src/game/mario.c")],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return result.stdout


class HyruleGateBuildTests(unittest.TestCase):
    def test_make_opt_in_preprocesses_gate_only_for_requested_hyrule_test_build(self):
        enabled_flags = make_version_flags(hyrule=1, diagnostics=1)
        self.assertIn("-DIOS_HYRULE_GATE_DIAGNOSTICS", enabled_flags)
        self.assertIn("-DIOS_HYRULE_WORLD", enabled_flags)
        self.assertIn("-DIOS_BUILD", enabled_flags)
        self.assertNotIn("-DDEBUG", enabled_flags)

        disabled_hyrule_flags = make_version_flags(hyrule=1, diagnostics=0)
        ordinary_flags = make_version_flags(hyrule=0, diagnostics=0)
        self.assertNotIn("-DIOS_HYRULE_GATE_DIAGNOSTICS", disabled_hyrule_flags)
        self.assertNotIn("-DIOS_HYRULE_GATE_DIAGNOSTICS", ordinary_flags)

        with tempfile.TemporaryDirectory(prefix="sm64-hyrule-gate-cpp-") as directory:
            root = Path(directory)
            enabled = preprocess_mario(enabled_flags, root / "enabled")
            disabled = preprocess_mario(disabled_hyrule_flags, root / "disabled-hyrule")
            ordinary = preprocess_mario(ordinary_flags, root / "ordinary")

        self.assertIn(GATE_FUNCTION, enabled)
        self.assertIn("mario_ios_update_hyrule_gate_debug(gMarioState)", enabled)
        self.assertNotIn(GATE_FUNCTION, disabled)
        self.assertNotIn("mario_ios_update_hyrule_gate_debug(gMarioState)", disabled)
        self.assertNotIn(GATE_FUNCTION, ordinary)
        self.assertNotIn("mario_ios_update_hyrule_gate_debug(gMarioState)", ordinary)

    def test_make_rejects_gate_diagnostics_outside_hyrule_ios_build(self):
        for target_ios, hyrule in ((1, 0), (0, 1)):
            result = subprocess.run(
                [
                    "make", "--no-print-directory", "-pn", "clean", "NOEXTRACT=1",
                    f"TARGET_IOS={target_ios}", "TARGET_OS_IOS=1", "RENDER_API=METAL",
                    f"IOS_HYRULE_WORLD={hyrule}", "IOS_HYRULE_GATE_DIAGNOSTICS=1",
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("IOS_HYRULE_GATE_DIAGNOSTICS=1 requires", result.stderr)

    def test_workflow_opts_in_diagnostics_only_for_hyrule_dispatch(self):
        workflow = (ROOT / ".github/workflows/ios-metal-ipa.yml").read_text(encoding="utf-8")
        build_script = (ROOT / "ios/build_ios_metal_ci.sh").read_text(encoding="utf-8")
        app_script = (ROOT / "build.sh").read_text(encoding="utf-8")
        expected = "IOS_HYRULE_GATE_DIAGNOSTICS: ${{ inputs.hyrule_o2r_release_tag != '' && '1' || '0' }}"
        self.assertIn(expected, workflow)
        self.assertIn('IOS_HYRULE_GATE_DIAGNOSTICS="${IOS_HYRULE_GATE_DIAGNOSTICS:-0}"', build_script)
        self.assertIn('IOS_HYRULE_GATE_DIAGNOSTICS="${IOS_HYRULE_GATE_DIAGNOSTICS:-0}"', app_script)


if __name__ == "__main__":
    unittest.main(verbosity=2)
