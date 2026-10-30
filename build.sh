#!/usr/bin/env bash
set -euo pipefail

if [[ ${SM64_SKIP_CODESIGN:-0} != 1 ]]; then
    codesign --remove-signature "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"
fi

case ${PLATFORM_NAME} in
    iphoneos)
        RENDER_API=METAL
        IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
        IOS_CC=$(xcrun --sdk iphoneos --find clang)
        IOS_CXX=$(xcrun --sdk iphoneos --find clang++)
        export CC="$IOS_CC -arch arm64 -mios-version-min=15.0 -fobjc-weak -isysroot $IOS_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
        export CXX="$IOS_CXX -arch arm64 -mios-version-min=15.0 -fobjc-weak -std=c++11 -isysroot $IOS_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
        export TARGET_OS_IOS=1
        rsync -aP --exclude="Info.plist" "sm64ios/sm64ios/" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"
        ;;
    iphonesimulator)
        RENDER_API=METAL
        IOS_SIM_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)
        IOS_SIM_CC=$(xcrun --sdk iphonesimulator --find clang)
        IOS_SIM_CXX=$(xcrun --sdk iphonesimulator --find clang++)
        export CC="$IOS_SIM_CC -arch $(uname -m) -mios-simulator-version-min=15.0 -fobjc-weak -isysroot $IOS_SIM_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
        export CXX="$IOS_SIM_CXX -arch $(uname -m) -mios-simulator-version-min=15.0 -fobjc-weak -std=c++11 -isysroot $IOS_SIM_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
        export TARGET_OS_IOS=1
        rsync -aP --exclude="Info.plist" "sm64ios/sm64ios/" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"
        ;;
    appletvos)
        RENDER_API=GL
        TV_SDK=$(xcrun --sdk appletvos --show-sdk-path)
        TV_CC=$(xcrun --sdk appletvos --find clang)
        TV_CXX=$(xcrun --sdk appletvos --find clang++)
        export CC="$TV_CC -arch arm64 -mtvos-version-min=13.2 -fobjc-weak -isysroot $TV_SDK -DUSE_GLES -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_TV"
        export CXX="$TV_CXX -arch arm64 -mtvos-version-min=13.2 -fobjc-weak -std=c++11 -isysroot $TV_SDK -DUSE_GLES -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_TV"
        export TARGET_OS_TV=1
        rsync -aP --exclude="Info.plist" "sm64ios/sm64tvos/" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"
        ;;
    appletvsimulator)
        RENDER_API=GL
        TV_SIM_SDK=$(xcrun --sdk appletvsimulator --show-sdk-path)
        TV_SIM_CC=$(xcrun --sdk appletvsimulator --find clang)
        TV_SIM_CXX=$(xcrun --sdk appletvsimulator --find clang++)
        export CC="$TV_SIM_CC -arch $(uname -m) -mtvos-simulator-version-min=13.2 -fobjc-weak -isysroot $TV_SIM_SDK -DUSE_GLES -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_TV"
        export CXX="$TV_SIM_CXX -arch $(uname -m) -mtvos-simulator-version-min=13.2 -fobjc-weak -std=c++11 -isysroot $TV_SIM_SDK -DUSE_GLES -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_TV"
        export TARGET_OS_TV=1
        rsync -aP --exclude="Info.plist" "sm64ios/sm64tvos/" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"
        ;;
esac

export IOSINCLUDE="${SDL2_INCLUDE_DIR}/"
export IOSLIBS="${BUILT_PRODUCTS_DIR}/"

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
"$MAKE_BIN" -j4 TARGET_IOS=1 RENDER_API="${RENDER_API:-GL}"

GAMEDIR_CP_SRC="build/us_pc/sm64ios"
if [[ ${EXTERNAL_DATA:-0} -eq 1 ]]; then
    GAMEDIR_CP_SRC="build/us_pc/."
fi
cp -R "$GAMEDIR_CP_SRC" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}/"
cp -R "build/us_pc/Base.lproj" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}/"
if [[ -e "build/us_pc/dynos" ]]; then
    cp -R "build/us_pc/dynos" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}/"
fi
rsync -aP --exclude="README.md" "ios/" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"

if [[ ${SM64_SKIP_CODESIGN:-0} != 1 ]]; then
    codesign --force --deep --sign "${CODE_SIGN_IDENTITY}" "${BUILT_PRODUCTS_DIR}/${FULL_PRODUCT_NAME}"
fi
