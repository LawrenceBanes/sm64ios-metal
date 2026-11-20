#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_bin=$(mktemp)
trap 'rm -f "$tmp_bin"' EXIT
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$repo_dir/src" \
    "$repo_dir/tests/test_gfx_postprocess.c" \
    "$repo_dir/src/pc/gfx/gfx_postprocess.c" -lm -o "$tmp_bin"
"$tmp_bin"
