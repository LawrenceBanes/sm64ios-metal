#!/usr/bin/env bash
set -euo pipefail

: "${CM_BUILD_DIR:?Codemagic checkout directory is required}"
: "${SM64_ASSET_GH_TOKEN:?Configure the secret SM64_ASSET_GH_TOKEN in the sm64_private_assets Codemagic group}"
: "${SDL2_REVISION:?Pinned SDL2 revision is required}"
: "${ASSET_BUNDLE_RELEASE_TAG:?Private asset release tag is required}"
[[ "${IOS_HYRULE_WORLD:-}" == 0 && "${IOS_CONNECTED_WORLD_TEST:-}" == 1 ]] || {
    echo "This workflow requires IOS_HYRULE_WORLD=0 and IOS_CONNECTED_WORLD_TEST=1" >&2
    exit 1
}
[[ "$ASSET_BUNDLE_RELEASE_TAG" =~ ^sm64-assets-[A-Za-z0-9._-]+$ ]] || {
    echo "Invalid private asset release tag" >&2
    exit 1
}

cd "$CM_BUILD_DIR"
build_diag_dir="$CM_BUILD_DIR/build/codemagic-connected-world"
product_dir="$build_diag_dir/ios-products/Release-iphoneos"
sdl_stage="$build_diag_dir/sdl2-stage"
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/sm64-codemagic.XXXXXX")
assets_extracted=0
cleanup() {
    status=$?
    trap - EXIT
    if [[ "$assets_extracted" == 1 ]]; then
        python3 ios/asset_bundle.py clean --root "$CM_BUILD_DIR" || true
    fi
    rm -rf -- "$temp_dir"
    exit "$status"
}
trap cleanup EXIT
mkdir -p "$build_diag_dir" "$product_dir"

{
    xcodebuild -version
    printf 'Xcode path: %s\n' "$(xcode-select -p)"
    printf 'iPhoneOS SDK: %s\n' "$(xcrun --sdk iphoneos --show-sdk-version)"
    printf 'Metal compiler: %s\n' "$(xcrun --sdk iphoneos --find metal)"
    printf 'Metal library linker: %s\n' "$(xcrun --sdk iphoneos --find metallib)"
} 2>&1 | tee "$build_diag_dir/toolchain.log"
[[ "$(xcodebuild -version | sed -n '1s/^Xcode //p')" == 27.1* ]] || {
    echo "Expected Xcode 27.1" >&2
    exit 1
}
[[ "$(xcrun --sdk iphoneos --show-sdk-version)" == 27.* ]] || {
    echo "Expected iPhoneOS SDK 27.x" >&2
    exit 1
}

if ! command -v gmake >/dev/null 2>&1 || ! gmake --version | head -n 1 | grep -Eq 'GNU Make ([4-9]|[1-9][0-9])\.'; then
    HOMEBREW_NO_AUTO_UPDATE=1 brew install make
fi
ios_objcopy=$(command -v gobjcopy || true)
if [[ -z "$ios_objcopy" ]]; then
    HOMEBREW_NO_AUTO_UPDATE=1 brew install binutils
    ios_objcopy="$(brew --prefix binutils)/bin/gobjcopy"
fi
[[ -x "$ios_objcopy" ]] || { echo "gobjcopy is unavailable" >&2; exit 1; }

export GH_TOKEN="$SM64_ASSET_GH_TOKEN"
metadata=$(gh release view "$ASSET_BUNDLE_RELEASE_TAG" \
    --repo LawrenceBanes/sm64ios-metal --json isDraft,body)
python3 -c 'import json,sys; d=json.loads(sys.argv[1]); assert d["isDraft"] and d["body"] == "Temporary private asset bundle for sm64ios-metal IPA build."' "$metadata"
gh release download "$ASSET_BUNDLE_RELEASE_TAG" \
    --repo LawrenceBanes/sm64ios-metal \
    --pattern 'sm64-assets.tar.gz' \
    --pattern 'sm64-assets.sha256' \
    --dir "$temp_dir"
expected_sha=$(python3 ios/asset_checksum.py "$temp_dir/sm64-assets.sha256" sm64-assets.tar.gz)
actual_sha=$(shasum -a 256 "$temp_dir/sm64-assets.tar.gz" | awk '{print $1}')
[[ "$actual_sha" == "$expected_sha" ]] || { echo "Private asset bundle SHA-256 mismatch" >&2; exit 1; }
assets_extracted=1
python3 ios/asset_bundle.py extract "$temp_dir/sm64-assets.tar.gz" --root "$CM_BUILD_DIR"
unset GH_TOKEN SM64_ASSET_GH_TOKEN

git init "$temp_dir/SDL2"
git -C "$temp_dir/SDL2" remote add origin https://github.com/libsdl-org/SDL.git
git -C "$temp_dir/SDL2" fetch --depth 1 origin "$SDL2_REVISION"
git -C "$temp_dir/SDL2" checkout --detach FETCH_HEAD
[[ "$(git -C "$temp_dir/SDL2" rev-parse HEAD)" == "$SDL2_REVISION" ]] || {
    echo "SDL2 revision mismatch" >&2
    exit 1
}
ios/build_sdl2_ios.sh "$temp_dir/SDL2" "$product_dir" "$sdl_stage" \
    2>&1 | tee "$build_diag_dir/sdl2-build.log"

METAL_VALIDATION_OUTPUT_DIR="$build_diag_dir/metal-validation" \
    ios/validate-metal-apple.sh 2>&1 | tee "$build_diag_dir/metal-validation.log"

SM64_SKIP_CODESIGN=1 \
NOEXTRACT=1 \
EXTERNAL_DATA=1 \
IOS_HYRULE_WORLD=0 \
IOS_CONNECTED_WORLD_TEST=1 \
IOS_OBJCOPY="$ios_objcopy" \
xcodebuild \
    -project sm64ios/sm64ios.xcodeproj \
    -scheme sm64ios \
    -configuration Release \
    -sdk iphoneos \
    -destination 'generic/platform=iOS' \
    -derivedDataPath "$build_diag_dir/DerivedData" \
    SYMROOT="$build_diag_dir/ios-products" \
    CONFIGURATION_BUILD_DIR="$product_dir" \
    SDL2_ROOT="$sdl_stage" \
    SDL2_INCLUDE_DIR="$sdl_stage/include" \
    IOS_HYRULE_WORLD=0 \
    IOS_CONNECTED_WORLD_TEST=1 \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    build 2>&1 | tee "$build_diag_dir/ios-build.log"

app="$product_dir/sm64ios.app"
ipa="$build_diag_dir/sm64ios-connected-world-arm64-unsigned.ipa"
[[ -d "$app" ]] || { echo "Xcode did not produce $app" >&2; exit 1; }
python3 ios/package_unsigned_ipa.py "$app" "$ipa"
python3 ios/validate_ipa.py "$ipa" --require-system-deps --require-game-textures
shasum -a 256 "$ipa" | tee "$build_diag_dir/ipa.sha256"
echo "Unsigned IPA structure validated; signing and device rendering remain unverified."
