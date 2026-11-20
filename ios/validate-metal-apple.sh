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
         "$repo_dir/src/pc/gfx/gfx_texture_override.c"
         "$repo_dir/src/pc/gfx/gfx_metal_shader.c"
         "$repo_dir/src/pc/gfx/gfx_shader64_pack.c"
         "$repo_dir/src/pc/gfx/gfx_metal_state.c"
         "$repo_dir/src/pc/gfx/gfx_scene_capture.c"
         "$repo_dir/src/pc/gfx/gfx_postprocess.c"
         "$repo_dir/src/pc/gfx/gfx_shadow_map.c"
         "$repo_dir/src/pc/gfx/gfx_cc.c"
         "$repo_dir/tests/metal_backend_diagnostics_stub.m"
         "$repo_dir/tests/metal_backend_config_stub.c"
         "$repo_dir/tests/metal_backend_link_main.m")
frameworks=(-framework Foundation -framework UIKit -framework Metal
            -framework MetalKit -framework QuartzCore -framework CoreGraphics)

echo "Compiling and linking the Metal backend against the iOS device SDK (unsigned validation executable)."
xcrun --sdk iphoneos clang "${common[@]}" -isysroot "$device_sdk" -arch arm64 \
    -mios-version-min=13.2 "${sources[@]}" "${frameworks[@]}" \
    -o "$device_dir/MetalBackendValidation"

echo "Compiling and linking the Metal backend against the iOS Simulator SDK (unsigned validation executable)."
xcrun --sdk iphonesimulator clang "${common[@]}" -isysroot "$simulator_sdk" -arch arm64 \
    -mios-simulator-version-min=13.2 "${sources[@]}" "${frameworks[@]}" \
    -o "$simulator_dir/MetalBackendValidation"

echo "Checking scene-package provider, Gfx bridge, and area hooks against the iOS SDK."
xcrun --sdk iphoneos clang "${common[@]}" -isysroot "$device_sdk" -arch arm64 \
    -mios-version-min=15.0 -fsyntax-only -DNON_MATCHING=1 -DAVOID_UB=1 \
    -DIOS_BUILD=1 -DTARGET_OS_IOS=1 -DVERSION_US=1 -DEXTERNAL_DATA=1 -DIOS_O2R_SCENE_RUNTIME=1 \
    -DIOS_SCENE_DIAGNOSTIC=1 \
    "$repo_dir/src/engine/world_scene.c" "$repo_dir/src/engine/world_scene_file.c" \
    "$repo_dir/src/engine/world_scene_view.c" \
    "$repo_dir/src/engine/world_region.c" "$repo_dir/src/engine/surface_load.c" \
    "$repo_dir/src/pc/gfx/gfx_pc.c" "$repo_dir/src/game/area.c" \
    "$repo_dir/src/game/rendering_graph_node.c" "$repo_dir/src/game/level_update.c" \
    "$repo_dir/levels/castle_grounds/script.c"

echo "Checking the Gameplay import command against the iOS SDK."
xcrun --sdk iphoneos clang "${common[@]}" -isysroot "$device_sdk" -arch arm64 \
    -mios-version-min=15.0 -fno-objc-arc -fobjc-weak -fsyntax-only -DNON_MATCHING=1 -DAVOID_UB=1 \
    -DIOS_BUILD=1 -DEXTERNAL_DATA=1 -DIOS_O2R_SCENE_RUNTIME=1 -DIOS_SCENE_DIAGNOSTIC=1 \
    "$repo_dir/src/pc/gfx/gfx_uikit.m"

echo "Generating representative shader variants."
host_sdk=$(xcrun --sdk macosx --show-sdk-path)
xcrun --sdk macosx clang -isysroot "$host_sdk" -std=gnu11 \
    -D_LANGUAGE_C -DF3DEX_GBI_2E=1 \
    -I"$repo_dir/include" -I"$repo_dir/src" -I"$repo_dir" \
    "$repo_dir/tests/metal_shader_emit.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_shader.c" "$repo_dir/src/pc/gfx/gfx_cc.c" \
    -o "$output_dir/metal_shader_emit"
"$output_dir/metal_shader_emit" "$shader_dir"

echo "Compiling generated Metal shaders, including SSAO, bloom, shadow depth, and screen-space light-shaft shaders, with the iOS Metal compiler and linking metallibs."
for source in "$shader_dir"/*.metal; do
    name=$(basename "$source" .metal)
    xcrun --sdk iphoneos metal -c "$source" -o "$shader_dir/$name.air"
    xcrun --sdk iphoneos metallib "$shader_dir/$name.air" -o "$shader_dir/$name.metallib"
    echo "compiled and linked $name"
done

echo "Compiling SHADER64 API 1 reference pack sources with the iPhoneOS Metal compiler."
wrapper_tool="$output_dir/shader64-wrap"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$wrapper_tool"
for source in "$repo_dir"/ios/shaderpacks/Classic64/shaders/*.metal \
             "$repo_dir"/ios/shaderpacks/Paper64/shaders/*.metal \
             "$repo_dir"/ios/shaderpacks/Lumen64/shaders/*.metal; do
    pack=$(basename "$(dirname "$(dirname "$source")")")
    name="shader64-$pack-$(basename "$source" .metal)"
    "$wrapper_tool" "$source" > "$shader_dir/$name.metal"
    xcrun --sdk iphoneos metal -c "$shader_dir/$name.metal" -o "$shader_dir/$name.air"
    xcrun --sdk iphoneos metallib "$shader_dir/$name.air" -o "$shader_dir/$name.metallib"
    echo "compiled and linked SHADER64 reference $name"
done

echo "Compiling SHADER64 API 2 graph-pass examples with the iPhoneOS Metal compiler."
wrapper_v2_tool="$output_dir/shader64-wrap-v2"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_v2.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$wrapper_v2_tool"
for source in "$repo_dir"/ios/shaderpacks/GraphDemo/shaders/*.metal \
             "$repo_dir"/ios/shaderpacks/BlissGridlines/shaders/*.metal \
             "$repo_dir"/ios/shaderpacks/DepthDebug64/shaders/*.metal \
             "$repo_dir"/ios/shaderpacks/MaterialDebug64/shaders/*.metal; do
    if [[ "$(basename "$source")" == "material.metal" || "$(basename "$source")" == "depth.metal" || "$(basename "$source")" == "normals.metal" || "$(basename "$source")" == "lighting.metal" ]]; then
        continue
    fi
    pack=$(basename "$(dirname "$(dirname "$source")")")
    name="shader64-api2-$pack-$(basename "$source" .metal)"
    "$wrapper_v2_tool" "$source" > "$shader_dir/$name.metal"
    xcrun --sdk iphoneos metal -c "$shader_dir/$name.metal" -o "$shader_dir/$name.air"
    xcrun --sdk iphoneos metallib "$shader_dir/$name.air" -o "$shader_dir/$name.metallib"
    echo "compiled and linked SHADER64 API 2 reference $name"
done

lit_source="$repo_dir/ios/shaderpacks/Lumen64Graph/shaders/lighting.metal"
lit_wrapper_tool="$output_dir/shader64-wrap-lit"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_lit_pass.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$lit_wrapper_tool"
"$lit_wrapper_tool" "$lit_source" > "$shader_dir/shader64-lumen64graph-lighting.metal"
xcrun --sdk iphoneos metal -c "$shader_dir/shader64-lumen64graph-lighting.metal" \
    -o "$shader_dir/shader64-lumen64graph-lighting.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-lumen64graph-lighting.air" \
    -o "$shader_dir/shader64-lumen64graph-lighting.metallib"
echo "compiled and linked SHADER64 Lumen64Graph directional-light pass"

shadow_wrapper_tool="$output_dir/shader64-wrap-shadow"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_shadow_pass.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -lm -o "$shadow_wrapper_tool"
shadow_source="$repo_dir/ios/shaderpacks/Lumen64ShadowGraph/shaders/shadows.metal"
"$shadow_wrapper_tool" "$shadow_source" > "$shader_dir/shader64-lumen64-shadowgraph-shadows.metal"
xcrun --sdk iphoneos metal -c "$shader_dir/shader64-lumen64-shadowgraph-shadows.metal" \
    -o "$shader_dir/shader64-lumen64-shadowgraph-shadows.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-lumen64-shadowgraph-shadows.air" \
    -o "$shader_dir/shader64-lumen64-shadowgraph-shadows.metallib"
echo "compiled and linked SHADER64 Lumen64ShadowGraph shadow-map pass"

texture_wrapper_tool="$output_dir/shader64-wrap-textured"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_textured_pass.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -lm -o "$texture_wrapper_tool"
texture_source="$repo_dir/ios/shaderpacks/TextureDemo64/shaders/textured.metal"
"$texture_wrapper_tool" "$texture_source" > "$shader_dir/shader64-textured-demo.metal"
xcrun --sdk iphoneos metal -c "$shader_dir/shader64-textured-demo.metal" \
    -o "$shader_dir/shader64-textured-demo.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-textured-demo.air" \
    -o "$shader_dir/shader64-textured-demo.metallib"
echo "compiled and linked SHADER64 TextureDemo64 user texture pass"

normal_texture_wrapper_tool="$output_dir/shader64-wrap-normal-textures"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_normals_textured_pass.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$normal_texture_wrapper_tool"
normal_texture_source="$repo_dir/ios/shaderpacks/NormalTexture64/shaders/normal_texture.metal"
"$normal_texture_wrapper_tool" "$normal_texture_source" > "$shader_dir/shader64-normal-texture.metal"
xcrun --sdk iphoneos metal -c "$shader_dir/shader64-normal-texture.metal" \
    -o "$shader_dir/shader64-normal-texture.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-normal-texture.air" \
    -o "$shader_dir/shader64-normal-texture.metallib"
echo "compiled and linked SHADER64 combined scene-normal and user-texture pass"

compute_source="$repo_dir/ios/shaderpacks/ComputeBlur64/shaders/blur.metal"
xcrun --sdk iphoneos metal -c "$compute_source" -o "$shader_dir/shader64-compute-blur.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-compute-blur.air" \
    -o "$shader_dir/shader64-compute-blur.metallib"
echo "compiled and linked SHADER64 bounded image compute reference"

compute_texture_source="$repo_dir/ios/shaderpacks/ComputeTexture64/shaders/filter.metal"
xcrun --sdk iphoneos metal -c "$compute_texture_source" -o "$shader_dir/shader64-compute-texture.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-compute-texture.air" \
    -o "$shader_dir/shader64-compute-texture.metallib"
echo "compiled and linked SHADER64 compute with user textures"

frame_wrapper_tool="$output_dir/shader64-wrap-frame-pass"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_frame_pass.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$frame_wrapper_tool"
frame_source="$repo_dir/ios/shaderpacks/AnimatedGraph64/shaders/pulse.metal"
"$frame_wrapper_tool" "$frame_source" > "$shader_dir/shader64-animated-graph.metal"
xcrun --sdk iphoneos metal -c "$shader_dir/shader64-animated-graph.metal" \
    -o "$shader_dir/shader64-animated-graph.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-animated-graph.air" \
    -o "$shader_dir/shader64-animated-graph.metallib"
echo "compiled and linked SHADER64 frame-aware fullscreen reference"

depth_compute_source="$repo_dir/ios/shaderpacks/DepthCompute64/shaders/fog.metal"
xcrun --sdk iphoneos metal -c "$depth_compute_source" -o "$shader_dir/shader64-depth-compute.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-depth-compute.air" \
    -o "$shader_dir/shader64-depth-compute.metallib"
echo "compiled and linked SHADER64 scene-depth compute reference"

depth_normals_compute_source="$repo_dir/ios/shaderpacks/DepthNormalsCompute64/shaders/surface.metal"
xcrun --sdk iphoneos metal -c "$depth_normals_compute_source" -o "$shader_dir/shader64-depth-normals-compute.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-depth-normals-compute.air" \
    -o "$shader_dir/shader64-depth-normals-compute.metallib"
echo "compiled and linked SHADER64 scene-depth plus scene-normals compute reference"

scene_data_compute_source="$repo_dir/ios/shaderpacks/SceneDataCompute64/shaders/surface.metal"
xcrun --sdk iphoneos metal -c "$scene_data_compute_source" -o "$shader_dir/shader64-scene-data-compute.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-scene-data-compute.air" \
    -o "$shader_dir/shader64-scene-data-compute.metallib"
echo "compiled and linked SHADER64 combined texture, depth, and normal compute reference"

material_wrapper_tool="$output_dir/shader64-wrap-material"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_material.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$material_wrapper_tool"
material_texture_wrapper_tool="$output_dir/shader64-wrap-material-textures"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_material_textures.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$material_texture_wrapper_tool"
for material_source in "$repo_dir"/ios/shaderpacks/GraphDemo/shaders/material.metal \
                      "$repo_dir"/ios/shaderpacks/MaterialDebug64/shaders/material.metal \
                      "$repo_dir"/ios/shaderpacks/Paper64MaterialGraph/shaders/material.metal; do
    pack=$(basename "$(dirname "$(dirname "$material_source")")")
    name="shader64-$pack-material"
    if [[ "$pack" == "Paper64MaterialGraph" ]]; then
        "$material_texture_wrapper_tool" "$material_source" > "$shader_dir/$name.metal"
    else
        "$material_wrapper_tool" "$material_source" > "$shader_dir/$name.metal"
    fi
    xcrun --sdk iphoneos metal -c "$shader_dir/$name.metal" -o "$shader_dir/$name.air"
    xcrun --sdk iphoneos metallib "$shader_dir/$name.air" -o "$shader_dir/$name.metallib"
    echo "compiled and linked SHADER64 API 2 $pack material hook"
done

depth_wrapper_tool="$output_dir/shader64-wrap-depth"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_depth_pass.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$depth_wrapper_tool"
for depth_source in "$repo_dir"/ios/shaderpacks/DepthDebug64/shaders/*.metal; do
    pack=$(basename "$(dirname "$(dirname "$depth_source")")")
    name="shader64-$pack-scene-depth"
    "$depth_wrapper_tool" "$depth_source" > "$shader_dir/$name.metal"
    xcrun --sdk iphoneos metal -c "$shader_dir/$name.metal" -o "$shader_dir/$name.air"
    xcrun --sdk iphoneos metallib "$shader_dir/$name.air" -o "$shader_dir/$name.metallib"
    echo "compiled and linked SHADER64 $pack scene-depth reference"
done

normal_wrapper_tool="$output_dir/shader64-wrap-normals"
xcrun --sdk macosx clang -std=c11 -I"$repo_dir/include" -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/shader64_wrap_normals_pass.c" "$repo_dir/src/pc/gfx/gfx_shader64_pack.c" \
    -o "$normal_wrapper_tool"
normal_source="$repo_dir/ios/shaderpacks/NormalDebug64/shaders/normals.metal"
"$normal_wrapper_tool" "$normal_source" > "$shader_dir/shader64-normal-debug.metal"
xcrun --sdk iphoneos metal -c "$shader_dir/shader64-normal-debug.metal" \
    -o "$shader_dir/shader64-normal-debug.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-normal-debug.air" \
    -o "$shader_dir/shader64-normal-debug.metallib"
echo "compiled and linked SHADER64 native scene-normal debug pass"

echo "Compiling the SHADER64 precompiled-library ABI example for iPhoneOS."
xcrun --sdk iphoneos metal -c "$repo_dir/ios/shaderpacks/examples/compiled_postprocess.metal" \
    -o "$shader_dir/shader64-compiled-example.air"
xcrun --sdk iphoneos metallib "$shader_dir/shader64-compiled-example.air" \
    -o "$shader_dir/shader64-compiled-example.metallib"
test -s "$shader_dir/shader64-compiled-example.metallib"
echo "compiled and linked SHADER64 precompiled-library example"

echo "Verifying project wiring and Makefile framework selection."
"$repo_dir/tests/check_metal_project_wiring.sh"

echo "Apple SDK compile/link and MSL validation passed. This did not run the executable or validate GPU rendering."
