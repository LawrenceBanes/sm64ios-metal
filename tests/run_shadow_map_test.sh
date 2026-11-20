#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-shadow-map-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$repo_dir/src" \
    "$repo_dir/tests/shadow_map_test.c" "$repo_dir/src/pc/gfx/gfx_shadow_map.c" -lm -o "$test_bin"
"$test_bin"
