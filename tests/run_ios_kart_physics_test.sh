#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-clang}
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-ios-kart.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"$cc" -std=c99 -Wall -Wextra -Werror -I"$repo_dir/include" \
    "$repo_dir/src/game/ios_kart.c" "$repo_dir/tests/ios_kart_physics_test.c" \
    -lm -o "$test_bin"
"$test_bin"
