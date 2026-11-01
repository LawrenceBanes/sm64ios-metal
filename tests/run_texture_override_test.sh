#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-cc}
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-texture-override-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"$cc" -std=c99 -Wall -Wextra -Werror -I"$repo_dir/src" \
    "$repo_dir/src/pc/gfx/gfx_texture_override.c" \
    "$repo_dir/tests/texture_override_test.c" -o "$test_bin"
"$test_bin"
