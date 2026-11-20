#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-gfx-rt-scene.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"${CC:-clang}" -std=c99 -Wall -Wextra -Werror -I"$repo_dir/src" \
    "$repo_dir/tests/test_gfx_rt_scene.c" "$repo_dir/src/pc/gfx/gfx_rt_scene.c" \
    -lm -o "$test_bin"
"$test_bin"
