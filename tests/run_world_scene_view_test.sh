#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-world-scene-view.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"${CC:-clang}" -std=c99 -Wall -Wextra -Werror -I"$repo_dir/src" \
    "$repo_dir/tests/test_world_scene_view.c" "$repo_dir/src/engine/world_scene_view.c" \
    -lm -o "$test_bin"
"$test_bin"
