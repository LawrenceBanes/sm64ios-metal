#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-clang}
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-texture-cache-reset-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"$cc" -std=c11 -Wall -Wextra -Werror "$repo_dir/tests/texture_cache_reset_test.c" -o "$test_bin"
"$test_bin"
