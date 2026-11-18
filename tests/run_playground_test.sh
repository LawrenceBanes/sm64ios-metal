#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
"${CC:-clang}" -std=gnu99 -DF3DEX_GBI_2 -DVERSION_US -DNON_MATCHING -DAVOID_UB -DIOS_BUILD -DIOS_HAT_MOVEMENT     -ffunction-sections -fdata-sections -Iinclude -Isrc -I. tests/test_playground.c src/pc/cheats.c src/engine/math_util.c     -Wl,--gc-sections -lm -o "$scratch/test"
"$scratch/test"
"${CC:-clang}" -fsyntax-only -std=gnu99 -DF3DEX_GBI_2 -DVERSION_US -DNON_MATCHING -DAVOID_UB -DIOS_BUILD     -Iinclude -Isrc -I. src/game/rendering_graph_node.c src/game/obj_behaviors.c
