#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-metal-shader-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/metal_shader_codegen_test.c" \
    "$repo_dir/src/pc/gfx/gfx_metal_shader.c" \
    "$repo_dir/src/pc/gfx/gfx_cc.c" -lm -o "$test_bin"
"$test_bin"
