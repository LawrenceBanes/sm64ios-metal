#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-cc}
case "$(uname -s)" in
    Darwin) gc_sections_flag=-Wl,-dead_strip ;;
    *) gc_sections_flag=-Wl,--gc-sections ;;
esac
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-fs-writepath-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"$cc" -std=c99 -Wall -Wextra -Wno-format-truncation -DNON_MATCHING=1 -DAVOID_UB=1 \
    -ffunction-sections -fdata-sections \
    "$gc_sections_flag" -I"$repo_dir/src" -I"$repo_dir/include" \
    "$repo_dir/src/pc/fs/fs.c" "$repo_dir/tests/fs_writepath_test.c" -o "$test_bin"
"$test_bin"
