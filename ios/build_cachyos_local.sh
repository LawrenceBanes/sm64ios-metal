#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

usage() {
    cat <<'EOF'
Usage:
  ios/build_cachyos_local.sh prepare <oot.o2r>
  ios/build_cachyos_local.sh package <built-app.app> <output.ipa>

prepare converts the supplied Hyrule Field and Kokiri Forest scenes, verifies the local
extracted SM64 assets, runs the Linux regression suite and IPA packager tests,
and compiles the Hyrule option plus the same-area connected-forest Castle
objects with the host compiler.

package wraps and validates an existing unsigned iPhoneOS .app bundle. It does
not build or sign that app. On CachyOS, the .app must have been built elsewhere
with Apple's iOS SDK and Xcode tools.
EOF
}

fail() {
    printf 'local iOS preflight: %s\n' "$*" >&2
    exit 1
}

command=${1:-}
case "$command" in
    prepare)
        [[ $# -eq 2 ]] || { usage >&2; exit 2; }
        OOT_ARCHIVE=$(realpath -- "$2")
        [[ -f "$OOT_ARCHIVE" && -r "$OOT_ARCHIVE" ]] || fail "cannot read supplied O2R archive: $2"
        cd "$ROOT"

        for tool in python3 make cc; do
            command -v "$tool" >/dev/null 2>&1 || fail "required host tool is missing: $tool"
        done
        make --version | head -n 1 | grep -Eq 'Make ([4-9]|[1-9][0-9]+)\.' || fail "GNU Make 4 or newer is required"

        printf 'Converting local Hyrule Field scene: %s\n' "$OOT_ARCHIVE"
        python3 tools/hyrule_importer.py "$OOT_ARCHIVE" build/hyrule_field_import
        cp build/hyrule_field_import/render/hyrule_field_runtime.inc.c build/hyrule_field_generated.inc.c
        cp build/hyrule_field_import/render/hyrule_field_spawn.h build/hyrule_field_spawn.h

        printf '\nConverting the Kokiri Forest default scene with the same importer...\n'
        python3 tools/hyrule_importer.py "$OOT_ARCHIVE" build/kokiri_forest_import \
            --scene-prefix scenes/shared/spot04_scene
        cc -fsyntax-only -std=gnu99 -D_LANGUAGE_C -DF3DEX_GBI_2 \
            -DNON_MATCHING=1 -DAVOID_UB=1 -I include -I src -I . \
            build/kokiri_forest_import/render/kokiri_forest_runtime.inc.c

        asset_bundle=$(mktemp "${TMPDIR:-/tmp}/sm64-local-assets.XXXXXX.tar.gz")
        asset_log=$(mktemp "${TMPDIR:-/tmp}/sm64-local-assets.XXXXXX.log")
        cleanup() { rm -f -- "$asset_bundle" "$asset_log"; }
        trap cleanup EXIT HUP INT TERM
        python3 ios/asset_bundle.py create "$asset_bundle" >"$asset_log"
        asset_count=$(sed -nE 's/^USA game asset files: ([0-9]+) \(ROM excluded\)$/\1/p' "$asset_log")
        [[ "$asset_count" == 1538 ]] || fail "expected the complete 1,538-file USA asset set; found ${asset_count:-none}"
        printf 'Private extracted assets verified locally: %s mapped files; temporary bundle will be removed.\n' "$asset_count"
        cleanup
        trap - EXIT HUP INT TERM

        printf '\nRunning Linux regression suite...\n'
        tests/run_linux_checks.sh

        printf '\nTesting local IPA packaging and validation code with synthetic fixtures...\n'
        (cd ios && python3 -m unittest -v test_ipa_validation.py test_asset_checksum.py)

        printf '\nCompiling Hyrule and shared level-transition C objects with host cc...\n'
        make -j4 \
            build/us_pc/levels/hyrule_field/leveldata.o \
            build/us_pc/levels/hyrule_field/geo.o \
            build/us_pc/levels/hyrule_field/script.o \
            build/us_pc/levels/castle_grounds/leveldata.o \
            build/us_pc/src/game/area.o \
            build/us_pc/src/game/level_update.o \
            build/us_pc/levels/scripts.o \
            VERSION=us TARGET_IOS=1 TARGET_OS_IOS=1 IOS_HYRULE_WORLD=1 \
            NOEXTRACT=1 RENDER_API=METAL IOSINCLUDE=include/

        printf '\nCompiling the same-area connected forest test without the Hyrule level flag...\n'
        make -j4 \
            build/us_pc/levels/castle_grounds/leveldata.o \
            build/us_pc/levels/castle_grounds/geo.o \
            build/us_pc/levels/castle_grounds/script.o \
            build/us_pc/src/engine/surface_load.o \
            build/us_pc/src/engine/surface_collision.o \
            build/us_pc/src/game/area.o \
            build/us_pc/src/game/mario.o \
            VERSION=us TARGET_IOS=1 TARGET_OS_IOS=1 IOS_CONNECTED_WORLD_TEST=1 \
            NOEXTRACT=1 RENDER_API=METAL IOSINCLUDE=include/

        printf '\nSwitching the reused build directory back to the ordinary Castle Grounds mode...\n'
        make -j4 \
            build/us_pc/levels/castle_grounds/leveldata.o \
            build/us_pc/levels/castle_grounds/geo.o \
            build/us_pc/levels/castle_grounds/script.o \
            build/us_pc/src/engine/surface_load.o \
            build/us_pc/src/engine/surface_collision.o \
            build/us_pc/src/game/area.o \
            build/us_pc/src/game/mario.o \
            build/us_pc/levels/scripts.o \
            VERSION=us TARGET_IOS=1 TARGET_OS_IOS=1 IOS_HYRULE_WORLD=0 \
            IOS_CONNECTED_WORLD_TEST=0 NOEXTRACT=1 RENDER_API=METAL IOSINCLUDE=include/

        printf '\nCachyOS preflight complete. These are Linux host objects, not iPhoneOS objects.\n'
        if command -v xcodebuild >/dev/null 2>&1 && command -v xcrun >/dev/null 2>&1; then
            printf 'Apple build tools are visible in PATH; this script still does not invoke them.\n'
        else
            printf 'No IPA was built: this host lacks Xcode/xcrun and the iPhoneOS SDK.\n'
        fi
        ;;
    package)
        [[ $# -eq 3 ]] || { usage >&2; exit 2; }
        APP=$(realpath -- "$2")
        IPA=$(realpath -m -- "$3")
        cd "$ROOT"
        python3 ios/package_unsigned_ipa.py "$APP" "$IPA"
        python3 ios/validate_ipa.py "$IPA" --require-game-textures
        sha256sum "$IPA"
        printf 'Package structure is validated; signing and on-device installation are not.\n'
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
