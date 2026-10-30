#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-cc}
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-metal-state-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"$cc" -std=c99 -Wall -Wextra -Werror -I"$repo_dir/include" -I"$repo_dir/src" \
    "$repo_dir/src/pc/gfx/gfx_metal_state.c" "$repo_dir/tests/metal_state_test.c" \
    -o "$test_bin"
"$test_bin"
