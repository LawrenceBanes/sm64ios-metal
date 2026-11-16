#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-world-region.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"${CC:-clang}" -std=gnu99 -Wall -Wextra -Werror -Wno-unused-variable \
    -ffunction-sections -fdata-sections -D_LANGUAGE_C -DF3DEX_GBI_2 \
    -DNON_MATCHING=1 -DAVOID_UB=1 \
    -DIOS_CONNECTED_WORLD_TEST=1 -I"$repo_dir/include" -I"$repo_dir/src" -I"$repo_dir" \
    "$repo_dir/tests/test_world_region.c" "$repo_dir/src/engine/world_region.c" \
    "$repo_dir/src/engine/surface_collision.c" -Wl,--gc-sections -lm -o "$test_bin"
"$test_bin"
