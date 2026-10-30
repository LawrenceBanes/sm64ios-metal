#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-scene-capture-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$repo_dir/src/pc/gfx" \
    "$repo_dir/tests/scene_capture_test.c" \
    "$repo_dir/src/pc/gfx/gfx_scene_capture.c" -o "$test_bin"
"$test_bin"
