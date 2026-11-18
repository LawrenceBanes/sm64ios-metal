#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM

LUA_SRC="$ROOT/vendor/lua53/src"
cp -R "$ROOT/tests/mods/fixtures" "$BUILD/fixtures"
cc -std=gnu99 -O1 -g -DNON_MATCHING=1 -DAVOID_UB=1 \
  -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT" -I"$LUA_SRC" \
  "$ROOT/tests/mods/host_runtime_test.c" \
  "$ROOT/src/pc/mods/mod_runtime.c" \
  "$LUA_SRC/lapi.c" "$LUA_SRC/lcode.c" "$LUA_SRC/lctype.c" "$LUA_SRC/ldebug.c" \
  "$LUA_SRC/ldo.c" "$LUA_SRC/ldump.c" "$LUA_SRC/lfunc.c" "$LUA_SRC/lgc.c" \
  "$LUA_SRC/llex.c" "$LUA_SRC/lmem.c" "$LUA_SRC/lobject.c" "$LUA_SRC/lopcodes.c" \
  "$LUA_SRC/lparser.c" "$LUA_SRC/lstate.c" "$LUA_SRC/lstring.c" "$LUA_SRC/ltable.c" \
  "$LUA_SRC/ltm.c" "$LUA_SRC/lundump.c" "$LUA_SRC/lvm.c" "$LUA_SRC/lzio.c" \
  "$LUA_SRC/lauxlib.c" "$LUA_SRC/lbaselib.c" "$LUA_SRC/lcorolib.c" \
  "$LUA_SRC/lmathlib.c" "$LUA_SRC/lstrlib.c" "$LUA_SRC/ltablib.c" "$LUA_SRC/lutf8lib.c" \
  -lm -o "$BUILD/mod_runtime_test"

"$BUILD/mod_runtime_test" "$BUILD/fixtures"
