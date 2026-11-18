#!/usr/bin/env bash
# Asset-free Metal/SDL2 validation, with an optional private full-game build.

cleanup() {
    local result=$?
    trap - EXIT
    if [[ ${assets_extracted:-0} == 1 ]]; then
        python3 ios/asset_bundle.py clean --root "$repo_dir" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$private_dir"
    if [[ ${GITHUB_ACTIONS:-} == true ]]; then
        rm -rf -- "$output_dir/SDL2"
        if [[ "$mode" == full || "$mode" == compile ]]; then
            rm -rf -- "$output_dir/ios-products" "$output_dir/DerivedData" "$sdl_stage/build"
        fi
    fi
    exit "$result"
}

check_toolchain() {
    {
        xcodebuild -version
        printf 'iPhoneOS SDK: %s\n' "$(xcrun --sdk iphoneos --show-sdk-version)"
        printf 'Metal compiler: %s\n' "$(xcrun --sdk iphoneos --find metal)"
        printf 'Metal library linker: %s\n' "$(xcrun --sdk iphoneos --find metallib)"
    } >"$safe_dir/toolchain.log" 2>&1
    cat "$safe_dir/toolchain.log"
    local version major minor
    version=$(xcodebuild -version | sed -n '1s/^Xcode //p')
    major=${version%%.*}
    minor=${version#*.}
    minor=${minor%%.*}
    [[ "$major" == 27 && "$minor" =~ ^[0-9]+$ && "$minor" -ge 1 ]] || {
        echo 'Xcode 27.1 or newer within major version 27 is required' >&2
        return 1
    }
    [[ $(xcrun --sdk iphoneos --show-sdk-version) == 27.* ]] || {
        echo 'An iPhoneOS 27.x SDK is required' >&2
        return 1
    }
}

validate_asset_free() {
    check_toolchain
    [[ "$sdl_revision" == 5d249570393f7a37e037abf22cd6012a4cc56a71 ]] || {
        echo 'SDL2 revision must match the scene-patch pin' >&2
        return 1
    }
    if [[ ! -f "$product_dir/libSDL2.a" || ! -f "$sdl_stage/include/SDL2/SDL_config.h" ]]; then
        git init -q "$output_dir/SDL2"
        git -C "$output_dir/SDL2" remote add origin https://github.com/libsdl-org/SDL.git
        git -C "$output_dir/SDL2" fetch --depth 1 origin "$sdl_revision"
        git -C "$output_dir/SDL2" checkout --detach -q FETCH_HEAD
        [[ $(git -C "$output_dir/SDL2" rev-parse HEAD) == "$sdl_revision" ]]
        ios/build_sdl2_ios.sh "$output_dir/SDL2" "$product_dir" "$sdl_stage" \
            >"$safe_dir/sdl2-build.log" 2>&1 || {
                tail -n 40 "$safe_dir/sdl2-build.log" >&2
                return 1
            }
    fi
    echo 'Pinned SDL2 iPhoneOS arm64 build: passed'

    METAL_VALIDATION_OUTPUT_DIR="$output_dir/metal-validation" \
        ios/validate-metal-apple.sh >"$safe_dir/metal-validation.log" 2>&1 || {
            tail -n 50 "$safe_dir/metal-validation.log" >&2
            return 1
        }
    echo 'iPhoneOS/simulator Metal backend and generated MSL validation: passed'
    printf 'SDL2 and Metal asset-free validation: PASS\n' >"$safe_dir/ci-status.txt"
}

install_game_tools() {
    if ! command -v gmake >/dev/null 2>&1 ||
       ! gmake --version | head -n 1 | grep -Eq 'GNU Make ([4-9]|[1-9][0-9])\.'; then
        HOMEBREW_NO_AUTO_UPDATE=1 brew install make
    fi
    ios_objcopy=$(command -v gobjcopy || true)
    if [[ -z "$ios_objcopy" ]]; then
        HOMEBREW_NO_AUTO_UPDATE=1 brew install binutils
        ios_objcopy="$(brew --prefix binutils)/bin/gobjcopy"
    fi
    [[ -x "$ios_objcopy" ]] || { echo 'gobjcopy is unavailable' >&2; return 1; }
}

download_private_assets() {
    : "${SM64_ASSET_PRIVATE_REPOSITORY:?Set the private owner/repo in the SM64_ASSET_PRIVATE_REPOSITORY Actions variable}"
    : "${SM64_PRIVATE_BUILD_GH_TOKEN:?Set a contents:write token scoped to the private build repository in the SM64_PRIVATE_BUILD_GH_TOKEN Actions secret}"
    : "${ASSET_BUNDLE_RELEASE_TAG:?Supply a private draft asset-bundle release tag}"
    [[ "$SM64_ASSET_PRIVATE_REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || {
        echo 'Private asset repository must be owner/repo' >&2
        return 1
    }
    [[ "$SM64_ASSET_PRIVATE_REPOSITORY" != LawrenceBanes/sm64ios-metal ]] || {
        echo 'Public source repository cannot be the asset repository' >&2
        return 1
    }
    [[ "$ASSET_BUNDLE_RELEASE_TAG" =~ ^sm64-assets-[A-Za-z0-9._-]+$ ]] || {
        echo 'Invalid private asset release tag' >&2
        return 1
    }
    local visibility metadata expected actual
    visibility=$(GH_TOKEN="$SM64_PRIVATE_BUILD_GH_TOKEN" gh api \
        "repos/$SM64_ASSET_PRIVATE_REPOSITORY" --jq '.private')
    [[ "$visibility" == true ]] || { echo 'Asset repository is not private' >&2; return 1; }
    metadata=$(GH_TOKEN="$SM64_PRIVATE_BUILD_GH_TOKEN" gh release view \
        "$ASSET_BUNDLE_RELEASE_TAG" --repo "$SM64_ASSET_PRIVATE_REPOSITORY" \
        --json isDraft,assets)
    python3 -c 'import json,sys; d=json.loads(sys.argv[1]); assert d["isDraft"] and {"sm64-assets.tar.gz", "sm64-assets.sha256"} <= {a["name"] for a in d["assets"]}' "$metadata"
    GH_TOKEN="$SM64_PRIVATE_BUILD_GH_TOKEN" gh release download \
        "$ASSET_BUNDLE_RELEASE_TAG" --repo "$SM64_ASSET_PRIVATE_REPOSITORY" \
        --pattern 'sm64-assets.tar.gz' --pattern 'sm64-assets.sha256' \
        --dir "$private_dir" >/dev/null
    expected=$(python3 ios/asset_checksum.py "$private_dir/sm64-assets.sha256" sm64-assets.tar.gz)
    actual=$(shasum -a 256 "$private_dir/sm64-assets.tar.gz" | awk '{print $1}')
    [[ "$actual" == "$expected" ]] || { echo 'Private asset checksum mismatch' >&2; return 1; }
    assets_extracted=1
    python3 ios/asset_bundle.py extract "$private_dir/sm64-assets.tar.gz" --root "$repo_dir" \
        >"$private_dir/asset-extraction.log" 2>&1
    echo 'Private asset bundle validated and extracted on the runner'
}

download_private_hyrule_archive() {
    [[ ${IOS_HYRULE_WORLD:-0} == 1 ]] || return 0
    : "${HYRULE_O2R_RELEASE_TAG:?Supply a private draft O2R release tag when IOS_HYRULE_WORLD=1}"
    [[ "$HYRULE_O2R_RELEASE_TAG" =~ ^hyrule-o2r-[A-Za-z0-9._-]+$ ]] || {
        echo 'Invalid private O2R release tag' >&2
        return 1
    }
    local metadata expected actual
    metadata=$(GH_TOKEN="$SM64_PRIVATE_BUILD_GH_TOKEN" gh release view \
        "$HYRULE_O2R_RELEASE_TAG" --repo "$SM64_ASSET_PRIVATE_REPOSITORY" \
        --json isDraft,assets)
    python3 -c 'import json,sys; d=json.loads(sys.argv[1]); assert d["isDraft"] and {"oot.o2r", "oot.o2r.sha256"} <= {a["name"] for a in d["assets"]}' "$metadata"
    GH_TOKEN="$SM64_PRIVATE_BUILD_GH_TOKEN" gh release download \
        "$HYRULE_O2R_RELEASE_TAG" --repo "$SM64_ASSET_PRIVATE_REPOSITORY" \
        --pattern 'oot.o2r' --pattern 'oot.o2r.sha256' \
        --dir "$private_dir" >/dev/null
    expected=$(python3 ios/asset_checksum.py "$private_dir/oot.o2r.sha256" oot.o2r)
    actual=$(shasum -a 256 "$private_dir/oot.o2r" | awk '{print $1}')
    [[ "$actual" == "$expected" ]] || { echo 'Private O2R checksum mismatch' >&2; return 1; }
    python3 - "$private_dir/oot.o2r" <<'PY'
import pathlib, sys, zipfile
archive = pathlib.Path(sys.argv[1])
if archive.stat().st_size > 512 * 1024 * 1024:
    raise SystemExit('Private O2R exceeds 512 MiB limit')
with zipfile.ZipFile(archive) as bundle:
    if bundle.testzip() is not None:
        raise SystemExit('Private O2R has a corrupt ZIP member')
    if 'scenes/shared/spot00_scene/spot00_scene' not in bundle.namelist():
        raise SystemExit('Private O2R lacks Hyrule Field scene')
PY
    export HYRULE_O2R_PATH="$private_dir/oot.o2r"
    echo 'Optional private Hyrule archive validated on the runner'
}

build_full_game() {
    [[ -f "$product_dir/libSDL2.a" && -f "$sdl_stage/include/SDL2/SDL_config.h" ]] || {
        echo 'Run asset-free validation first to build pinned SDL2' >&2
        return 1
    }
    [[ ${IOS_HYRULE_WORLD:-0} == 0 || ${IOS_HYRULE_WORLD:-0} == 1 ]] || {
        echo 'IOS_HYRULE_WORLD must be 0 or 1' >&2
        return 1
    }
    [[ ${IOS_CONNECTED_WORLD_TEST:-1} == 1 ]] || {
        echo 'This CI target requires IOS_CONNECTED_WORLD_TEST=1' >&2
        return 1
    }
    [[ ${IOS_HYRULE_GATE_DIAGNOSTICS:-0} == 0 || ${IOS_HYRULE_GATE_DIAGNOSTICS:-0} == 1 ]] || {
        echo 'IOS_HYRULE_GATE_DIAGNOSTICS must be 0 or 1' >&2
        return 1
    }
    [[ ${IOS_HYRULE_GATE_DIAGNOSTICS:-0} == 0 || ${IOS_HYRULE_WORLD:-0} == 1 ]] || {
        echo 'IOS_HYRULE_GATE_DIAGNOSTICS=1 requires IOS_HYRULE_WORLD=1' >&2
        return 1
    }
    local connected_world=${IOS_CONNECTED_WORLD:-0}
    [[ "$connected_world" == 0 || "$connected_world" == 1 ]] || {
        echo 'IOS_CONNECTED_WORLD must be 0 or 1' >&2
        return 1
    }
    if [[ "$connected_world" == 1 &&
          ( ${IOS_HYRULE_WORLD:-0} != 1 || -z ${HYRULE_O2R_RELEASE_TAG:-} ) ]]; then
        echo 'IOS_CONNECTED_WORLD=1 requires the private Hyrule O2R release tag and IOS_HYRULE_WORLD=1' >&2
        return 1
    fi
    local hat_movement=${IOS_HAT_MOVEMENT:-0}
    [[ "$hat_movement" == 0 || "$hat_movement" == 1 ]] || {
        echo 'IOS_HAT_MOVEMENT must be 0 or 1' >&2
        return 1
    }
    download_private_assets
    download_private_hyrule_archive
    local private_build_token=$SM64_PRIVATE_BUILD_GH_TOKEN
    unset SM64_PRIVATE_BUILD_GH_TOKEN
    install_game_tools
    local raw_log="$private_dir/ios-build.log"
    echo "iOS gate diagnostic compile flag: ${IOS_HYRULE_GATE_DIAGNOSTICS:-0}"
    echo "iOS hat movement compile flag: $hat_movement"
    echo "iOS connected world compile flag: $connected_world"
    if ! SM64_SKIP_CODESIGN=1 NOEXTRACT=1 EXTERNAL_DATA=1 \
        IOS_HYRULE_WORLD="${IOS_HYRULE_WORLD:-0}" IOS_HYRULE_GATE_DIAGNOSTICS="${IOS_HYRULE_GATE_DIAGNOSTICS:-0}" IOS_CONNECTED_WORLD_TEST=1 IOS_OBJCOPY="$ios_objcopy" \
        IOS_HAT_MOVEMENT="$hat_movement" IOS_CONNECTED_WORLD="$connected_world" \
        xcodebuild -project sm64ios/sm64ios.xcodeproj -scheme sm64ios \
        -configuration Release -sdk iphoneos -destination 'generic/platform=iOS' \
        -derivedDataPath "$output_dir/DerivedData" \
        SYMROOT="$output_dir/ios-products" CONFIGURATION_BUILD_DIR="$product_dir" \
        SDL2_ROOT="$sdl_stage" SDL2_INCLUDE_DIR="$sdl_stage/include" \
        IOS_HYRULE_WORLD="${IOS_HYRULE_WORLD:-0}" IOS_HYRULE_GATE_DIAGNOSTICS="${IOS_HYRULE_GATE_DIAGNOSTICS:-0}" IOS_CONNECTED_WORLD_TEST=1 \
        IOS_HAT_MOVEMENT="$hat_movement" IOS_CONNECTED_WORLD="$connected_world" \
        CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build \
        >"$raw_log" 2>&1; then
        python3 ios/ci_summarize_build_log.py "$raw_log" >"$safe_dir/ios-build-summary.txt"
        cat "$safe_dir/ios-build-summary.txt" >&2
        return 1
    fi
    echo 'Full iPhoneOS arm64 Metal game compilation: passed'
    local app="$product_dir/sm64ios.app"
    local ipa="$private_dir/sm64ios-connected-world-arm64-unsigned.ipa"
    [[ -d "$app" ]] || { echo 'Xcode did not create the app bundle' >&2; return 1; }
    python3 ios/package_unsigned_ipa.py "$app" "$ipa" >"$private_dir/packaging.log" 2>&1
    python3 ios/validate_ipa.py "$ipa" --require-system-deps --require-game-textures \
        >"$private_dir/ipa-validation.log" 2>&1
    local validation_counts
    validation_counts=$(grep -E '^(app bundle file entries|runtime resource pack file entries|build intermediates):' \
        "$private_dir/ipa-validation.log")
    local digest
    digest=$(shasum -a 256 "$ipa" | awk '{print $1}')
    upload_private_ipa "$ipa" "$digest" "$private_build_token"
    unset private_build_token
    printf 'Full game compile: PASS\nUnsigned IPA packaging and structural validation: PASS\nPrivate IPA draft release upload: PASS\n%s\nIPA SHA-256: %s\n' \
        "$validation_counts" "$digest" >"$safe_dir/ios-build-summary.txt"
    cat "$safe_dir/ios-build-summary.txt"
    echo 'IPA was uploaded only to the configured private draft release; no game app, raw full-build log, or extracted asset is uploaded.'
}

compile_full_game() {
    [[ -f "$product_dir/libSDL2.a" && -f "$sdl_stage/include/SDL2/SDL_config.h" ]] || {
        echo 'Run asset-free validation first to build pinned SDL2' >&2
        return 1
    }
    download_private_assets
    unset SM64_PRIVATE_BUILD_GH_TOKEN
    install_game_tools
    local raw_log="$private_dir/ios-compile.log"
    if ! SM64_SKIP_CODESIGN=1 NOEXTRACT=1 EXTERNAL_DATA=1 \
        IOS_HYRULE_WORLD=0 IOS_HYRULE_GATE_DIAGNOSTICS=0 IOS_CONNECTED_WORLD_TEST=1 IOS_OBJCOPY="$ios_objcopy" \
        IOS_HAT_MOVEMENT=0 IOS_CONNECTED_WORLD=0 \
        xcodebuild -project sm64ios/sm64ios.xcodeproj -scheme sm64ios \
        -configuration Release -sdk iphoneos -destination 'generic/platform=iOS' \
        -derivedDataPath "$output_dir/DerivedData" \
        SYMROOT="$output_dir/ios-products" CONFIGURATION_BUILD_DIR="$product_dir" \
        SDL2_ROOT="$sdl_stage" SDL2_INCLUDE_DIR="$sdl_stage/include" \
        IOS_HYRULE_WORLD=0 IOS_HYRULE_GATE_DIAGNOSTICS=0 IOS_CONNECTED_WORLD_TEST=1 \
        IOS_HAT_MOVEMENT=0 IOS_CONNECTED_WORLD=0 \
        CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build \
        >"$raw_log" 2>&1; then
        python3 ios/ci_summarize_build_log.py "$raw_log" >"$safe_dir/ios-compile-summary.txt"
        cat "$safe_dir/ios-compile-summary.txt" >&2
        return 1
    fi
    [[ -d "$product_dir/sm64ios.app" ]] || { echo 'Xcode did not create the app bundle' >&2; return 1; }
    python3 ios/ci_summarize_build_log.py "$raw_log" >"$safe_dir/ios-compile-summary.txt"
    printf 'Full iPhoneOS arm64 Metal game compilation: PASS\n' >>"$safe_dir/ios-compile-summary.txt"
    cat "$safe_dir/ios-compile-summary.txt"
    echo 'Compile-only validation passed. No IPA was packaged or uploaded.'
}

upload_private_ipa() {
    local ipa=$1 digest=$2 token=$3 repository tag visibility checksum_file
    : "${SM64_IPA_PRIVATE_REPOSITORY:?Set the private owner/repo in the SM64_IPA_PRIVATE_REPOSITORY Actions variable}"
    [[ -n "$token" ]] || { echo 'Private build token is unavailable for IPA delivery' >&2; return 1; }
    [[ "$SM64_IPA_PRIVATE_REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || {
        echo 'Private IPA destination must be owner/repo' >&2
        return 1
    }
    [[ "$SM64_IPA_PRIVATE_REPOSITORY" != LawrenceBanes/sm64ios-metal ]] || {
        echo 'Public source repository cannot be the IPA destination' >&2
        return 1
    }
    [[ "$SM64_IPA_PRIVATE_REPOSITORY" == "$SM64_ASSET_PRIVATE_REPOSITORY" ]] || {
        echo 'Asset and IPA storage must use the same repository for this single-token setup' >&2
        return 1
    }
    repository=$SM64_IPA_PRIVATE_REPOSITORY
    visibility=$(GH_TOKEN="$token" gh api "repos/$repository" --jq '.private')
    [[ "$visibility" == true ]] || { echo 'IPA destination repository is not private' >&2; return 1; }
    [[ ${GITHUB_RUN_ID:-} =~ ^[0-9]+$ && ${GITHUB_RUN_ATTEMPT:-} =~ ^[0-9]+$ ]] || {
        echo 'GitHub run identifiers are required to create a unique private draft release' >&2
        return 1
    }
    tag="sm64-ios-unsigned-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
    checksum_file="$private_dir/$(basename "$ipa").sha256"
    printf '%s  %s\n' "$digest" "$(basename "$ipa")" >"$checksum_file"
    GH_TOKEN="$token" gh release create "$tag" \
        --repo "$repository" --draft \
        --title "SM64 iOS unsigned test build ${GITHUB_RUN_ID}.${GITHUB_RUN_ATTEMPT}" \
        --notes "Connected-world test build from commit ${GITHUB_SHA:-unknown}. Unsigned IPA; device behavior remains unverified." \
        >"$private_dir/private-release-create.log"
    GH_TOKEN="$token" gh release upload "$tag" \
        "$ipa" "$checksum_file" --repo "$repository" \
        >"$private_dir/private-release-upload.log"
}

main() {
    set -euo pipefail
    mode=${1:-validate}
    [[ "$mode" == validate || "$mode" == compile || "$mode" == full ]] || {
        echo 'usage: ios/build_ios_metal_ci.sh validate|compile|full' >&2
        exit 2
    }

    repo_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
    cd "$repo_dir"
    output_dir=${CI_OUTPUT_DIR:-"${RUNNER_TEMP:-$repo_dir/build}/sm64-ios-ci"}
    safe_dir="$output_dir/safe"
    private_dir="$output_dir/private"
    product_dir="$output_dir/ios-products/Release-iphoneos"
    sdl_stage="$output_dir/sdl2-stage"
    sdl_revision=${SDL2_REVISION:-5d249570393f7a37e037abf22cd6012a4cc56a71}
    mkdir -p "$safe_dir" "$private_dir" "$product_dir"
    trap cleanup EXIT

    if [[ "$mode" == validate ]]; then
        validate_asset_free
    elif [[ "$mode" == compile ]]; then
        compile_full_game
    else
        build_full_game
    fi
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    main "$@"
fi
