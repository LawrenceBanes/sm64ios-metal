IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
IOS_CC=$(xcrun --sdk iphoneos --find clang)
IOS_CXX=$(xcrun --sdk iphoneos --find clang++)
export CC="$IOS_CC -arch arm64 -mios-version-min=13.2 -fobjc-weak -isysroot $IOS_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
export CXX="$IOS_CXX -arch arm64 -mios-version-min=13.2 -fobjc-weak -std=c++11 -isysroot $IOS_SDK -DAAPI_SDL2 -DWAPI_SDL2 -DTARGET_OS_IOS"
SDL2_ROOT=${SDL2_ROOT:-../SDL2}
export IOSINCLUDE="${IOSINCLUDE:-$SDL2_ROOT/include}"
export IOSLIBS="${IOSLIBS:-$SDL2_ROOT/lib}"
export CODE_SIGN_IDENTITY="Apple Development"
export CODE_SIGN_ENTITLEMENTS="sm64ios.entitlements"

gmake ios -j4 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL
