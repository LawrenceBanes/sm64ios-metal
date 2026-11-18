#!/usr/bin/env bash
set -e

IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
IOS_CC=$(xcrun --sdk iphoneos --find clang)
IOS_CXX=$(xcrun --sdk iphoneos --find clang++)
export CC="$IOS_CC -arch arm64 -mios-version-min=15.0 -fobjc-weak -isysroot $IOS_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
export CXX="$IOS_CXX -arch arm64 -mios-version-min=15.0 -fobjc-weak -std=c++11 -isysroot $IOS_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
SDL2_ROOT=${SDL2_ROOT:-../SDL2}
export IOSINCLUDE="${IOSINCLUDE:-$SDL2_ROOT/include}"
export IOSLIBS="${IOSLIBS:-$SDL2_ROOT/lib}"
export CODE_SIGN_IDENTITY="Apple Development"
export CODE_SIGN_ENTITLEMENTS="sm64ios.entitlements"

IOS_CONNECTED_WORLD_BUILD=${IOS_CONNECTED_WORLD:-0}
HYRULE_IMPORT_FLAG=
case "$IOS_CONNECTED_WORLD_BUILD" in
    0) ;;
    1) HYRULE_IMPORT_FLAG=--connected-world ;;
    *) echo 'IOS_CONNECTED_WORLD must be 0 or 1' >&2; exit 1 ;;
esac
IOS_HYRULE_WORLD_BUILD=0
if [[ ${IOS_HYRULE_WORLD:-0} == 1 || -n ${HYRULE_O2R_PATH:-} || "$IOS_CONNECTED_WORLD_BUILD" == 1 ]]; then
    if [[ -z ${HYRULE_O2R_PATH:-} || ! -f ${HYRULE_O2R_PATH} ]]; then
        echo "IOS_HYRULE_WORLD requires HYRULE_O2R_PATH to name a readable user-supplied .o2r archive." >&2
        exit 1
    fi
    HYRULE_IMPORT_DIR="build/hyrule_field_import"
    python3 tools/hyrule_importer.py "$HYRULE_O2R_PATH" "$HYRULE_IMPORT_DIR" ${HYRULE_IMPORT_FLAG:+"$HYRULE_IMPORT_FLAG"}
    cp "$HYRULE_IMPORT_DIR/render/hyrule_field_runtime.inc.c" build/hyrule_field_generated.inc.c
    cp "$HYRULE_IMPORT_DIR/render/hyrule_field_spawn.h" build/hyrule_field_spawn.h
    if [[ "$IOS_CONNECTED_WORLD_BUILD" == 1 ]]; then
        for file in hyrule_field_connection.h hyrule_field_connected_collision.inc.c hyrule_field_connector.inc.c; do
            cp "$HYRULE_IMPORT_DIR/render/$file" "build/$file"
        done
    fi
    IOS_HYRULE_WORLD_BUILD=1
fi
IOS_CONNECTED_WORLD_TEST_BUILD=${IOS_CONNECTED_WORLD_TEST:-0}
if [[ "$IOS_CONNECTED_WORLD_TEST_BUILD" != 0 && "$IOS_CONNECTED_WORLD_TEST_BUILD" != 1 ]]; then
    echo "IOS_CONNECTED_WORLD_TEST must be 0 or 1" >&2
    exit 1
fi

MAKE_BIN=$(command -v gmake || command -v make || true)
if [[ -z "$MAKE_BIN" ]]; then
    echo "GNU Make is required to build the game" >&2
    exit 1
fi
MAKE_VERSION=$("$MAKE_BIN" --version | sed -nE '1s/.*Make ([0-9]+)\.([0-9]+).*/\1 \2/p')
if [[ -z "$MAKE_VERSION" ]]; then
    echo "Unable to determine GNU Make version from $MAKE_BIN" >&2
    exit 1
fi
MAKE_MAJOR=${MAKE_VERSION%% *}
if (( MAKE_MAJOR < 4 )); then
    echo "GNU Make 4 or newer is required (found $MAKE_VERSION)" >&2
    exit 1
fi
"$MAKE_BIN" ios -j4 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL \
    IOS_HYRULE_WORLD="$IOS_HYRULE_WORLD_BUILD" \
    IOS_HYRULE_GATE_DIAGNOSTICS="${IOS_HYRULE_GATE_DIAGNOSTICS:-0}" \
    IOS_HAT_MOVEMENT="${IOS_HAT_MOVEMENT:-0}" \
    IOS_CONNECTED_WORLD="$IOS_CONNECTED_WORLD_BUILD" \
    IOS_CONNECTED_WORLD_TEST="$IOS_CONNECTED_WORLD_TEST_BUILD"
