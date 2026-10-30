#!/bin/bash
set -euo pipefail

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir=${METAL_VALIDATION_OUTPUT_DIR:-"${TMPDIR:-/tmp}/sm64-metal-apple-validation"}
device_dir="$output_dir/ios-device"
simulator_dir="$output_dir/ios-simulator"
shader_dir="$output_dir/generated-shaders"
mkdir -p "$device_dir" "$simulator_dir" "$shader_dir"

echo "SDK: $(xcrun --sdk iphoneos --show-sdk-version)"
echo "Xcode: $(xcodebuild -version | tr '\n' ' ')"

device_sdk=$(xcrun --sdk iphoneos --show-sdk-path)
simulator_sdk=$(xcrun --sdk iphonesimulator --show-sdk-path)
common=(-std=gnu11 -D_LANGUAGE_C -DF3DEX_GBI_2E=1 -DRAPI_METAL=1
        -I"$repo_dir/include" -I"$repo_dir/src" -I"$repo_dir")
sources=("$repo_dir/src/pc/gfx/gfx_metal.m"
         "$repo_dir/src/pc/gfx/gfx_metal_shader.c"
         "$repo_dir/src/pc/gfx/gfx_metal_state.c"
         "$repo_dir/src/pc/gfx/gfx_cc.c"
         "$repo_dir/tests/metal_backend_config_stub.c"
         "$repo_dir/tests/metal_backend_link_main.m")
frameworks=(-framework Foundation -framework UIKit -framework Metal
            -framework MetalKit -framework QuartzCore)

echo "Compiling and linking the Metal backend against the iOS device SDK (unsigned validation executable)."
xcrun --sdk iphoneos clang "${common[@]}" -isysroot "$device_sdk" -arch arm64 \
    -mios-version-min=13.2 "${sources[@]}" "${frameworks[@]}" \
    -o "$device_dir/MetalBackendValidation"

echo "Compiling and linking the Metal backend against the iOS Simulator SDK (unsigned validation executable)."
xcrun --sdk iphonesimulator clang "${common[@]}" -isysroot "$simulator_sdk" -arch arm64 \
    -mios-simulator-version-min=13.2 "${sources[@]}" "${frameworks[@]}" \
    -o "$simulator_dir/MetalBackendValidation"

echo "Generating representative shader variants."
host_sdk=$(xcrun --sdk macosx --show-sdk-path)
xcrun --sdk macosx clang -isysroot "$host_sdk" -std=gnu11 \
    -D_LANGUAGE_C -DF3DEX_GBI_2E=1 \
    -I"$repo_dir/include" -I"$repo_dir/src" -I"$repo_dir" \
    "$repo_dir/tests/metal_shader_emit.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_shader.c" "$repo_dir/src/pc/gfx/gfx_cc.c" \
    -o "$output_dir/metal_shader_emit"
"$output_dir/metal_shader_emit" "$shader_dir"

echo "Compiling generated Metal shaders with the iOS Metal compiler and linking metallibs."
for source in "$shader_dir"/*.metal; do
    name=$(basename "$source" .metal)
    xcrun --sdk iphoneos metal -c "$source" -o "$shader_dir/$name.air"
    xcrun --sdk iphoneos metallib "$shader_dir/$name.air" -o "$shader_dir/$name.metallib"
    echo "compiled and linked $name"
done

echo "Verifying project wiring and Makefile framework selection."
"$repo_dir/tests/check_metal_project_wiring.sh"

echo "Apple SDK compile/link and MSL validation passed. This did not run the executable or validate GPU rendering."
