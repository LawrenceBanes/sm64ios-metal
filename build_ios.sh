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

MAKE_BIN=$(command -v gmake || command -v make || true)
if [[ -z "$MAKE_BIN" ]]; then
    echo "GNU Make is required to build the game" >&2
    exit 1
fi
"$MAKE_BIN" ios -j4 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL
