#!/usr/bin/env python3
"""Follow the connected option through the runner, Xcode phase, and real Make flags."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/ios-metal-ipa.yml"

# Only the Apple tools and asset staging are stand-ins. The project's two
# shell scripts, GNU Make variable expansion and host C compilation run here.
HARNESS = r'''
set -euo pipefail
source "$SOURCE_ROOT/ios/build_ios_metal_ci.sh"
output_dir="$PWD/runner"
product_dir="$output_dir/products"
sdl_stage="$output_dir/sdl"
safe_dir="$output_dir/safe"
private_dir="$output_dir/private"
mkdir -p "$product_dir" "$sdl_stage/include/SDL2" "$safe_dir" "$private_dir"
touch "$product_dir/libSDL2.a" "$sdl_stage/include/SDL2/SDL_config.h"
SM64_PRIVATE_BUILD_GH_TOKEN=fixture
download_private_assets() { :; }
download_private_hyrule_archive() {
    if [[ ${IOS_HYRULE_WORLD:-0} == 1 ]]; then
        export HYRULE_O2R_PATH="$private_dir/oot.o2r"
        touch "$HYRULE_O2R_PATH"
    fi
}
install_game_tools() { ios_objcopy=fixture; }
xcrun() { printf '%s\n' fixture; }
rsync() { :; }
python3() {
    if [[ $1 == tools/hyrule_importer.py ]]; then
        printf '%s\n' "$@" > "$PWD/importer-args"
        mkdir -p "$3/render"
        touch "$3/render/hyrule_field_runtime.inc.c" "$3/render/hyrule_field_spawn.h"
        if [[ ${4:-} == --connected-world ]]; then
            cat > "$3/render/hyrule_field_connection.h" <<'EOF'
#define HYRULE_CONNECTION_X (-13200)
#define HYRULE_CONNECTION_Y 260
#define HYRULE_CONNECTION_Z 1460
#define HYRULE_CONNECTION_YAW (-90)
EOF
            touch "$3/render/hyrule_field_connected_collision.inc.c" "$3/render/hyrule_field_connector.inc.c"
        fi
    elif [[ $1 == ios/ci_summarize_build_log.py ]]; then
        cat "$2"
    else
        return 99
    fi
}
gmake() {
    if [[ $1 == --version ]]; then
        command make --version
        return
    fi
    command make -C "$SOURCE_ROOT" --no-print-directory -pn clean NOEXTRACT=1 "$@" > "$PWD/make-db"
    local flags
    flags=$(sed -n 's/^CFLAGS := //p' "$PWD/make-db")
    printf '%s\n' "$flags" > "$PWD/compiler-flags"
    "$HOST_CC" -std=gnu99 -DF3DEX_GBI_2E $flags -I"$PWD" -I"$SOURCE_ROOT/include" -I"$SOURCE_ROOT/src"         -I"$SOURCE_ROOT" -c "$SOURCE_ROOT/levels/castle_grounds/geo.c" -o "$PWD/geo.o"
    # Stop here: no app staging, packaging or publishing in a host test.
    return 97
}
xcodebuild() {
    printf '%s\n' "${IOS_CONNECTED_WORLD-unset}" > "$PWD/xcode-env"
    printf '%s\n' "${IOS_O2R_SCENE_RUNTIME-unset}" > "$PWD/xcode-o2r-env"
    printf '%s\n' "${IOS_SCENE_DIAGNOSTIC-unset}" > "$PWD/xcode-diagnostic-env"
    printf '%s\n' "$@" > "$PWD/xcode-args"
    local setting
    for setting; do
        case "$setting" in
            IOS_*=*|SDL2_INCLUDE_DIR=*) export "$setting" ;;
        esac
    done
    PLATFORM_NAME=iphoneos BUILT_PRODUCTS_DIR="$product_dir" FULL_PRODUCT_NAME=sm64ios.app         bash "$SOURCE_ROOT/build.sh"
}
export -f xcrun rsync python3 gmake
build_full_game
'''


class ConnectedWorldCITests(unittest.TestCase):
    def test_workflow_opt_in_defaults_off(self):
        workflow = WORKFLOW.read_text()
        self.assertRegex(workflow, r"(?m)^      connected_world:\n(?:        .+\n)+?        default: false$")
        self.assertIn("      IOS_CONNECTED_WORLD: '0'", workflow)
        self.assertIn(
            "IOS_CONNECTED_WORLD: ${{ github.event_name == 'workflow_dispatch' && inputs.connected_world && '1' || '0' }}",
            workflow,
        )

    def run_build_path(self, connected, hyrule, with_tag, scene_runtime=0, scene_diagnostic=0):
        fixture = tempfile.TemporaryDirectory(prefix="sm64-connected-build-")
        self.addCleanup(fixture.cleanup)
        work = Path(fixture.name)
        env = {
            **os.environ, "SOURCE_ROOT": str(ROOT), "HOST_CC": os.environ.get("CC", "clang"),
            "IOS_HYRULE_WORLD": str(hyrule), "IOS_HYRULE_GATE_DIAGNOSTICS": str(hyrule),
            "IOS_HAT_MOVEMENT": str(hyrule), "IOS_CONNECTED_WORLD_TEST": "1",
            "IOS_O2R_SCENE_RUNTIME": str(scene_runtime),
            "IOS_SCENE_DIAGNOSTIC": str(scene_diagnostic),
        }
        for key in ("HYRULE_O2R_PATH", "HYRULE_O2R_RELEASE_TAG", "IOS_CONNECTED_WORLD"):
            env.pop(key, None)
        if connected is not None:
            env["IOS_CONNECTED_WORLD"] = str(connected)
        if with_tag:
            env["HYRULE_O2R_RELEASE_TAG"] = "hyrule-o2r-fixture"
        result = subprocess.run(["bash", "-s"], cwd=work, env=env, text=True, input=HARNESS, capture_output=True)
        # The stub stops before staging, or the real runner rejects the mode.
        self.assertNotEqual(result.returncode, 0)
        return work, result

    def test_combined_xcode_configuration_reaches_compilation(self):
        work, result = self.run_build_path(1, 1, True)
        self.assertEqual((work / "xcode-env").read_text().strip(), "1", result.stderr)
        self.assertEqual((work / "xcode-o2r-env").read_text().strip(), "0", result.stderr)
        self.assertIn("IOS_CONNECTED_WORLD=1", (work / "xcode-args").read_text().splitlines())
        self.assertIn("IOS_O2R_SCENE_RUNTIME=0", (work / "xcode-args").read_text().splitlines())
        self.assertIn("--connected-world", (work / "importer-args").read_text().splitlines())
        flags = (work / "compiler-flags").read_text().split()
        for flag in ("-DIOS_CONNECTED_WORLD", "-DIOS_CONNECTED_WORLD_TEST", "-DIOS_HYRULE_WORLD",
                     "-DIOS_HYRULE_GATE_DIAGNOSTICS", "-DIOS_HAT_MOVEMENT"):
            self.assertIn(flag, flags)
        symbols = subprocess.check_output(["nm", "-u", work / "geo.o"], text=True)
        self.assertIn("hyrule_field_opaque_dl", symbols)
        self.assertIn("castle_grounds_hyrule_connector_dl", symbols)

    def test_ordinary_compile_defaults_off(self):
        work, result = self.run_build_path(None, 0, False)
        self.assertEqual((work / "xcode-env").read_text().strip(), "0", result.stderr)
        self.assertEqual((work / "xcode-o2r-env").read_text().strip(), "0", result.stderr)
        self.assertIn("IOS_CONNECTED_WORLD=0", (work / "xcode-args").read_text().splitlines())
        self.assertNotIn("-DIOS_CONNECTED_WORLD", (work / "compiler-flags").read_text().split())
        self.assertNotIn("-DIOS_O2R_SCENE_RUNTIME", (work / "compiler-flags").read_text().split())
        self.assertNotIn("-DIOS_SCENE_DIAGNOSTIC", (work / "compiler-flags").read_text().split())
        symbols = subprocess.check_output(["nm", "-u", work / "geo.o"], text=True)
        self.assertNotIn("hyrule_field_opaque_dl", symbols)
        self.assertNotIn("scene_diagnostic_geo", subprocess.check_output(["nm", work / "geo.o"], text=True))

    def test_connected_compile_requires_hyrule_and_private_tag(self):
        for hyrule, has_tag in ((0, True), (1, False)):
            with self.subTest(hyrule=hyrule, has_tag=has_tag):
                work, result = self.run_build_path(1, hyrule, has_tag)
                self.assertIn("IOS_CONNECTED_WORLD=1 requires the private Hyrule O2R release tag", result.stderr)
                self.assertFalse((work / "xcode-args").exists())

    def test_scene_runtime_opt_in_reaches_ios_compiler(self):
        work, result = self.run_build_path(None, 0, False, scene_runtime=1)
        self.assertEqual((work / "xcode-o2r-env").read_text().strip(), "1", result.stderr)
        self.assertIn("IOS_O2R_SCENE_RUNTIME=1", (work / "xcode-args").read_text().splitlines())
        self.assertIn("-DIOS_O2R_SCENE_RUNTIME", (work / "compiler-flags").read_text().split())

    def test_scene_diagnostic_lands_in_the_native_level(self):
        work, result = self.run_build_path(None, 0, False, scene_runtime=1, scene_diagnostic=1)
        self.assertEqual((work / "xcode-diagnostic-env").read_text().strip(), "1", result.stderr)
        self.assertIn("IOS_SCENE_DIAGNOSTIC=1", (work / "xcode-args").read_text().splitlines())
        self.assertIn("-DIOS_SCENE_DIAGNOSTIC", (work / "compiler-flags").read_text().split())
        self.assertIn("scene_diagnostic_geo", subprocess.check_output(["nm", work / "geo.o"], text=True))
        self.assertFalse((work / "importer-args").exists())

    def test_scene_diagnostic_requires_scene_runtime(self):
        work, result = self.run_build_path(None, 0, False, scene_diagnostic=1)
        self.assertIn("requires IOS_O2R_SCENE_RUNTIME=1", result.stderr)
        self.assertFalse((work / "xcode-args").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
