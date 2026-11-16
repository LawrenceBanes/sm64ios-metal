#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-surface-bounds.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"${CC:-clang}" -std=gnu99 -Wno-unused-function -Wno-unused-variable \
    -ffunction-sections -fdata-sections -D_LANGUAGE_C -DF3DEX_GBI_2 \
    -DNON_MATCHING=1 -DAVOID_UB=1 -DIOS_CONNECTED_WORLD_TEST=1 \
    -I"$repo_dir/include" -I"$repo_dir/src" -I"$repo_dir" \
    "$repo_dir/tests/test_surface_load_bounds.c" -Wl,--gc-sections -lm -o "$test_bin"
"$test_bin"
