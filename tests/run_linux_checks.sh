#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-clang}
SDL2_CFLAGS=$(pkg-config --cflags sdl2)

CC="$cc" "$repo_dir/tests/run_metal_shader_codegen_test.sh"
CC="$cc" "$repo_dir/tests/run_metal_state_test.sh"
CC="$cc" "$repo_dir/tests/run_scene_capture_test.sh"
CC="$cc" "$repo_dir/tests/run_scene_bvh_test.sh"
CC="$cc" "$repo_dir/tests/run_texture_cache_reset_test.sh"
python3 "$repo_dir/ios/test_render_diagnostics.py"
python3 "$repo_dir/ios/test_metal_graphics_foundation.py"

common_flags="-fsyntax-only -std=gnu99 -D_LANGUAGE_C -DF3DEX_GBI_2 -DWAPI_SDL2=1 -DAAPI_SDL2=1 -DCAPI_SDL2=1 -DHAVE_SDL2=1 -I$repo_dir/include -I$repo_dir/src -I$repo_dir"
# shellcheck disable=SC2086
$cc $common_flags -DRAPI_GL=1 $SDL2_CFLAGS \
    "$repo_dir/src/pc/gfx/gfx_scene_capture.c" "$repo_dir/src/pc/gfx/gfx_scene_bvh.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_state.c" "$repo_dir/src/pc/gfx/gfx_pc.c"
# shellcheck disable=SC2086
$cc $common_flags -DRAPI_METAL=1 -DTARGET_OS_IOS=1 $SDL2_CFLAGS \
    "$repo_dir/src/pc/gfx/gfx_scene_capture.c" "$repo_dir/src/pc/gfx/gfx_scene_bvh.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_state.c" "$repo_dir/src/pc/gfx/gfx_pc.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_shader.c"
# The Objective-C Metal file must stay excluded cleanly on non-Apple hosts.
$cc -x objective-c -fsyntax-only -std=gnu99 -DRAPI_METAL=1 \
    "$repo_dir/src/pc/gfx/gfx_metal.m"

make -C "$repo_dir/tools" -j2
