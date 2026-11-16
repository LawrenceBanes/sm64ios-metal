#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

usage() {
    cat <<'EOF'
Usage:
  ios/build_local_macos_ipa.sh <local-SDL2-checkout> <local-oot.o2r> [output.ipa]

Builds the unsigned arm64 iPhoneOS app locally with Xcode, from an already
available SDL2 checkout containing the pinned revision and local extracted
SM64/OoT assets. Set IOS_CONNECTED_WORLD_TEST=1 to include the Castle Grounds
same-area forest branch experiment. It makes no GitHub requests and does not
sign the app.
EOF
}

fail() {
    printf 'local macOS IPA build: %s\n' "$*" >&2
    exit 1
}

if [[ $(uname -s) != Darwin ]]; then
    fail "requires macOS with Xcode; use build_cachyos_local.sh prepare on CachyOS"
fi
[[ $# -ge 2 && $# -le 3 ]] || { usage >&2; exit 2; }

SDL2_SOURCE=$(cd -- "$1" && pwd)
OOT_ARCHIVE=$(cd -- "$(dirname -- "$2")" && pwd)/$(basename -- "$2")
[[ -d "$SDL2_SOURCE/.git" || -f "$SDL2_SOURCE/.git" ]] || fail "SDL2 source must be a local Git checkout"
[[ -f "$OOT_ARCHIVE" && -r "$OOT_ARCHIVE" ]] || fail "cannot read local OoT archive: $OOT_ARCHIVE"

CONNECTED_WORLD_MODE=${IOS_CONNECTED_WORLD_TEST:-0}
[[ "$CONNECTED_WORLD_MODE" == 0 || "$CONNECTED_WORLD_MODE" == 1 ]] \
    || fail "IOS_CONNECTED_WORLD_TEST must be 0 or 1"

for tool in xcodebuild xcrun git python3 gmake gobjcopy; do
    command -v "$tool" >/dev/null 2>&1 || fail "required local tool is missing: $tool"
done

XCODE_VERSION=$(xcodebuild -version | sed -n '1s/^Xcode //p')
[[ "$XCODE_VERSION" == 27.* ]] || fail "expected Xcode 27.x, found ${XCODE_VERSION:-unknown}"
IOS_SDK_VERSION=$(xcrun --sdk iphoneos --show-sdk-version)
[[ "$IOS_SDK_VERSION" == 27.* ]] || fail "expected iPhoneOS SDK 27.x, found $IOS_SDK_VERSION"

MAKE_VERSION=$(gmake --version | sed -nE '1s/.*Make ([0-9]+)\.([0-9]+).*/\1 \2/p')
[[ -n "$MAKE_VERSION" && ${MAKE_VERSION%% *} -ge 4 ]] || fail "GNU Make 4 or newer is required"

SDL2_PIN=$(sed -nE 's/^SDL2_PIN=([0-9a-f]{40})$/\1/p' "$SCRIPT_DIR/build_sdl2_ios.sh")
[[ -n "$SDL2_PIN" ]] || fail "could not read the pinned SDL2 revision from build_sdl2_ios.sh"
git -C "$SDL2_SOURCE" cat-file -e "$SDL2_PIN^{commit}" 2>/dev/null \
    || fail "local SDL2 checkout does not contain required commit $SDL2_PIN"

OUTPUT_IPA=${3:-"$ROOT/build/local-iphoneos/sm64ios-arm64-unsigned.ipa"}
if [[ "$OUTPUT_IPA" != /* ]]; then OUTPUT_IPA="$ROOT/$OUTPUT_IPA"; fi
BUILD_ROOT="$ROOT/build/local-iphoneos"
PRODUCT_DIR="$BUILD_ROOT/products/Release-iphoneos"
SDL_STAGE="$BUILD_ROOT/sdl2-stage"
DERIVED_DATA="$BUILD_ROOT/DerivedData"
mkdir -p "$BUILD_ROOT" "$PRODUCT_DIR"

asset_bundle=$(mktemp "${TMPDIR:-/tmp}/sm64-local-assets.XXXXXX.tar.gz")
asset_log=$(mktemp "${TMPDIR:-/tmp}/sm64-local-assets.XXXXXX.log")
cleanup() { rm -f -- "$asset_bundle" "$asset_log"; }
trap cleanup EXIT HUP INT TERM
cd "$ROOT"
python3 ios/asset_bundle.py create "$asset_bundle" >"$asset_log" \
    || fail "local extracted game assets are incomplete; extract the legally obtained baserom first"
asset_count=$(sed -nE 's/^USA game asset files: ([0-9]+) \(ROM excluded\)$/\1/p' "$asset_log")
[[ "$asset_count" == 1538 ]] || fail "expected 1,538 local USA assets; found ${asset_count:-none}"
cleanup
trap - EXIT HUP INT TERM

# Build the SDL patch in a unique temporary checkout; leave the user's SDL tree untouched.
SDL_WORKTREE=$(mktemp -d "${TMPDIR:-/tmp}/sm64-sdl2-ios.XXXXXX")
trap 'rm -rf -- "$SDL_WORKTREE"' EXIT HUP INT TERM
git clone --quiet --no-hardlinks --no-checkout "$SDL2_SOURCE" "$SDL_WORKTREE"
git -C "$SDL_WORKTREE" checkout --quiet --detach "$SDL2_PIN"

IOS_OBJCOPY=$(command -v gobjcopy)
"$SCRIPT_DIR/build_sdl2_ios.sh" "$SDL_WORKTREE" "$PRODUCT_DIR" "$SDL_STAGE" \
    2>&1 | tee "$BUILD_ROOT/sdl2-build.log"

SM64_SKIP_CODESIGN=1 \
NOEXTRACT=1 \
EXTERNAL_DATA=1 \
IOS_HYRULE_WORLD=1 \
IOS_CONNECTED_WORLD_TEST="$CONNECTED_WORLD_MODE" \
HYRULE_O2R_PATH="$OOT_ARCHIVE" \
IOS_OBJCOPY="$IOS_OBJCOPY" \
xcodebuild \
    -project sm64ios/sm64ios.xcodeproj \
    -scheme sm64ios \
    -configuration Release \
    -sdk iphoneos \
    -destination 'generic/platform=iOS' \
    -derivedDataPath "$DERIVED_DATA" \
    SYMROOT="$BUILD_ROOT/products" \
    CONFIGURATION_BUILD_DIR="$PRODUCT_DIR" \
    SDL2_ROOT="$SDL_STAGE" \
    SDL2_INCLUDE_DIR="$SDL_STAGE/include" \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    build 2>&1 | tee "$BUILD_ROOT/ios-build.log"

APP="$PRODUCT_DIR/sm64ios.app"
[[ -d "$APP" ]] || fail "Xcode succeeded without producing $APP"
python3 ios/package_unsigned_ipa.py "$APP" "$OUTPUT_IPA"
python3 ios/validate_ipa.py "$OUTPUT_IPA" --require-system-deps --require-game-textures
shasum -a 256 "$OUTPUT_IPA"
printf 'Unsigned IPA created locally. SideStore signing and iPhone launch remain unverified.\n'
