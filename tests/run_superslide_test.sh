#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
for hat in "" "-DIOS_HAT_MOVEMENT"; do
    "${CC:-clang}" -std=gnu99 -DF3DEX_GBI_2 -DVERSION_US -DNON_MATCHING -DAVOID_UB -DIOS_BUILD $hat \
        -ffunction-sections -fdata-sections -Iinclude -Isrc -I. \
        tests/test_superslide.c src/game/mario_superslide.c src/pc/cheats.c src/engine/math_util.c \
        -Wl,--gc-sections -lm -o "$scratch/test"
    "$scratch/test"
done
