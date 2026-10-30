#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 SDL2_SOURCE_DIR BUILT_PRODUCTS_DIR SDL2_STAGE_ROOT" >&2
    exit 2
fi

SDL2_SOURCE=$(cd "$1" && pwd)
BUILT_PRODUCTS=$(mkdir -p "$2" && cd "$2" && pwd)
SDL2_STAGE=$(mkdir -p "$3" && cd "$3" && pwd)
SDL2_PIN=5d249570393f7a37e037abf22cd6012a4cc56a71
ACTUAL_PIN=$(git -C "$SDL2_SOURCE" rev-parse HEAD)
if [[ "$ACTUAL_PIN" != "$SDL2_PIN" ]]; then
    echo "SDL2 checkout is not pinned revision $SDL2_PIN (got $ACTUAL_PIN)" >&2
    exit 1
fi

PROJECT="$SDL2_SOURCE/Xcode/SDL/SDL.xcodeproj"
if [[ ! -d "$PROJECT" ]]; then
    echo "SDL2 Xcode project not found at $PROJECT" >&2
    exit 1
fi

BUILD_ROOT="$SDL2_STAGE/build"
mkdir -p "$BUILD_ROOT"
xcodebuild \
    -project "$PROJECT" \
    -target "Static Library-iOS" \
    -configuration Release \
    -sdk iphoneos \
    -arch arm64 \
    IPHONEOS_DEPLOYMENT_TARGET=15.0 \
    SYMROOT="$BUILD_ROOT/Products" \
    OBJROOT="$BUILD_ROOT/Intermediates" \
    ONLY_ACTIVE_ARCH=NO \
    CODE_SIGNING_ALLOWED=NO \
    build

find_archive() {
    local archive_name=$1
    local result
    result=$(find "$BUILD_ROOT/Products" -type f -name "$archive_name" -print -quit)
    if [[ -z "$result" ]]; then
        echo "SDL2 build did not produce $archive_name" >&2
        return 1
    fi
    printf '%s\n' "$result"
}

SDL2_ARCHIVE=$(find_archive libSDL2.a)
cp "$SDL2_ARCHIVE" "$BUILT_PRODUCTS/libSDL2.a"
if ! xcrun ar -t "$BUILT_PRODUCTS/libSDL2.a" | grep 'SDL_uikit_main' >/dev/null; then
    echo "SDL2 iOS archive is missing its UIKit entry point" >&2
    exit 1
fi
mkdir -p "$SDL2_STAGE/include/SDL2"
cp -R "$SDL2_SOURCE/include/." "$SDL2_STAGE/include/SDL2/"
test -f "$SDL2_STAGE/include/SDL2/SDL_config.h"

xcrun --sdk iphoneos lipo -verify_arch arm64 "$BUILT_PRODUCTS/libSDL2.a"
echo "SDL2 $SDL2_PIN built for iPhoneOS arm64"
