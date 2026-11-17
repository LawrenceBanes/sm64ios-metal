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
CC="$cc" "$repo_dir/tests/run_texture_override_test.sh"
CC="$cc" "$repo_dir/tests/run_fs_writepath_test.sh"
python3 "$repo_dir/ios/test_render_diagnostics.py"
python3 "$repo_dir/ios/test_metal_graphics_foundation.py"
python3 "$repo_dir/ios/test_hd_texture_overrides.py"
python3 "$repo_dir/tests/check_ios_gameplay_features.py"
python3 "$repo_dir/tests/check_ios_kart_integration.py"
python3 "$repo_dir/tests/check_ios_portal_integration.py"
python3 "$repo_dir/tests/test_hyrule_importer.py"
python3 "$repo_dir/ios/test_hyrule_gateway.py"
python3 "$repo_dir/ios/test_connected_world.py"
python3 "$repo_dir/ios/test_hyrule_asset_workflow.py"
python3 "$repo_dir/ios/test_hyrule_gate.py"
python3 "$repo_dir/tests/test_hyrule_gate_build.py"
python3 "$repo_dir/ios/test_surface_partition_bounds.py"
CC="$cc" sh "$repo_dir/tests/run_world_region_test.sh"
CC="$cc" sh "$repo_dir/tests/run_surface_load_bounds_test.sh"
CC="$cc" "$repo_dir/tests/run_ios_kart_physics_test.sh"
CC="$cc" sh "$repo_dir/tests/run_ios_hat_test.sh"
CC="$cc" "$repo_dir/tests/run_ios_reality_test.sh"
python3 "$repo_dir/tests/check_ios_reality_integration.py"
portal_test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-portal-math.XXXXXX")
trap 'rm -f "$portal_test_bin"' EXIT HUP INT TERM
$cc -std=c99 -Wall -Wextra -I"$repo_dir/include" "$repo_dir/tests/test_portal_math.c" -lm -o "$portal_test_bin"
"$portal_test_bin"
rm -f "$portal_test_bin"
hyrule_gate_test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-hyrule-gate.XXXXXX")
$cc -std=c99 -Wall -Wextra -I"$repo_dir/include" -I"$repo_dir/src" \
    "$repo_dir/tests/test_hyrule_gate.c" -o "$hyrule_gate_test_bin"
"$hyrule_gate_test_bin"
rm -f "$hyrule_gate_test_bin"
trap - EXIT HUP INT TERM

common_flags="-fsyntax-only -std=gnu99 -D_LANGUAGE_C -DF3DEX_GBI_2 -DWAPI_SDL2=1 -DAAPI_SDL2=1 -DCAPI_SDL2=1 -DHAVE_SDL2=1 -I$repo_dir/include -I$repo_dir/src -I$repo_dir"
# Compile the gameplay-side iOS kart adapter on Linux so its action, collision,
# level actor, and behavior integration cannot regress behind source-only checks.
$cc $common_flags -DNON_MATCHING=1 -DAVOID_UB=1 -DIOS_BUILD=1 \
    "$repo_dir/src/game/ios_kart.c" "$repo_dir/src/game/mario.c" \
    "$repo_dir/src/game/mario_actions_moving.c" "$repo_dir/src/game/mario_actions_airborne.c" \
    "$repo_dir/src/game/mario_step.c" "$repo_dir/src/game/ios_reality.c" \
    "$repo_dir/src/game/object_list_processor.c" "$repo_dir/src/game/obj_behaviors.c" \
    "$repo_dir/data/behavior_data.c" "$repo_dir/levels/castle_grounds/script.c" \
    "$repo_dir/actors/group0_geo.c"
# The original kart model is a separate display-list unit; compile it with the
# same GBI declarations used by the actor group.
$cc $common_flags -DNON_MATCHING=1 -DAVOID_UB=1 -DIOS_BUILD=1 \
    -include sm64.h -include geo_commands.h "$repo_dir/actors/ios_kart/model.inc.c"
# Keep the ordinary SM64 display-list model syntax valid on host builds.
$cc $common_flags -DNON_MATCHING=1 -DAVOID_UB=1 -DIOS_BUILD=1 \
    -include sm64.h -include geo_commands.h "$repo_dir/actors/portal/model.inc.c"
# shellcheck disable=SC2086
$cc $common_flags -DRAPI_GL=1 $SDL2_CFLAGS \
    "$repo_dir/src/pc/gfx/gfx_scene_capture.c" "$repo_dir/src/pc/gfx/gfx_scene_bvh.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_state.c" "$repo_dir/src/pc/gfx/gfx_texture_override.c" \
    "$repo_dir/src/pc/gfx/gfx_pc.c"
# shellcheck disable=SC2086
$cc $common_flags -DRAPI_METAL=1 -DTARGET_OS_IOS=1 $SDL2_CFLAGS \
    "$repo_dir/src/pc/gfx/gfx_scene_capture.c" "$repo_dir/src/pc/gfx/gfx_scene_bvh.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_state.c" "$repo_dir/src/pc/gfx/gfx_texture_override.c" \
    "$repo_dir/src/pc/gfx/gfx_pc.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_shader.c"
# Check the iOS/Metal external-texture replacement path without requiring Apple frameworks.
# shellcheck disable=SC2086
$cc $common_flags -DRAPI_METAL=1 -DTARGET_OS_IOS=1 -DEXTERNAL_DATA=1 $SDL2_CFLAGS \
    "$repo_dir/src/pc/gfx/gfx_scene_capture.c" "$repo_dir/src/pc/gfx/gfx_scene_bvh.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_state.c" "$repo_dir/src/pc/gfx/gfx_texture_override.c" \
    "$repo_dir/src/pc/gfx/gfx_pc.c" "$repo_dir/src/pc/gfx/gfx_metal_shader.c"
# The Objective-C Metal file must stay excluded cleanly on non-Apple hosts.
$cc -x objective-c -fsyntax-only -std=gnu99 -DRAPI_METAL=1 \
    "$repo_dir/src/pc/gfx/gfx_metal.m"

make -C "$repo_dir/tools" -j2
