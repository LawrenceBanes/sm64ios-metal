#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-clang}
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-ios-reality.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"$cc" -std=c99 -Wall -Wextra -Werror -I"$repo_dir/include" -I"$repo_dir/src" \
    "$repo_dir/src/game/ios_reality.c" "$repo_dir/tests/ios_reality_test.c" -o "$test_bin"
"$test_bin"
printf '%s\n' 'iOS Reality Debugger runtime checks passed'
