#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-cc}
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-fs-writepath-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"$cc" -std=c99 -Wall -Wextra -Wno-format-truncation -DNON_MATCHING=1 -DAVOID_UB=1 \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -I"$repo_dir/src" -I"$repo_dir/include" \
    "$repo_dir/src/pc/fs/fs.c" "$repo_dir/tests/fs_writepath_test.c" -o "$test_bin"
"$test_bin"
