#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM

LUA_SRC="$ROOT/vendor/lua53/src"
cp -R "$ROOT/tests/mods/fixtures" "$BUILD/fixtures"
# MOD_TEST_CFLAGS is intentionally split into compiler arguments so this same
# host fixture can be run with sanitizers without changing its build command.
${CC:-cc} -std=gnu99 -O1 -g ${MOD_TEST_CFLAGS:-} -D_LANGUAGE_C -DF3DEX_GBI_2 -DMOD_RUNTIME_REAL_ZIP \
  -DNON_MATCHING=1 -DAVOID_UB=1 \
  -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT" -I"$LUA_SRC" \
  "$ROOT/tests/mods/host_runtime_test.c" \
  "$ROOT/src/pc/mods/mod_runtime.c" \
  "$ROOT/src/pc/fs/fs_packtype_zip.c" "$ROOT/src/pc/fs/dirtree.c" \
  "$LUA_SRC/lapi.c" "$LUA_SRC/lcode.c" "$LUA_SRC/lctype.c" "$LUA_SRC/ldebug.c" \
  "$LUA_SRC/ldo.c" "$LUA_SRC/ldump.c" "$LUA_SRC/lfunc.c" "$LUA_SRC/lgc.c" \
  "$LUA_SRC/llex.c" "$LUA_SRC/lmem.c" "$LUA_SRC/lobject.c" "$LUA_SRC/lopcodes.c" \
  "$LUA_SRC/lparser.c" "$LUA_SRC/lstate.c" "$LUA_SRC/lstring.c" "$LUA_SRC/ltable.c" \
  "$LUA_SRC/ltm.c" "$LUA_SRC/lundump.c" "$LUA_SRC/lvm.c" "$LUA_SRC/lzio.c" \
  "$LUA_SRC/lauxlib.c" "$LUA_SRC/lbaselib.c" "$LUA_SRC/lcorolib.c" \
  "$LUA_SRC/lmathlib.c" "$LUA_SRC/lstrlib.c" "$LUA_SRC/ltablib.c" "$LUA_SRC/lutf8lib.c" \
  -lm -o "$BUILD/mod_runtime_test"

"$BUILD/mod_runtime_test" "$BUILD/fixtures"
printf '%s  %s\n' \
  7001744cb4023be31460960a0e4ff060f4311f573ccdac880564deb532617a3d \
  "$ROOT/tests/mods/real/sm64coop-achievements-2.1.0.zip" | sha256sum -c -
mkdir -p "$BUILD/real-mod"
"$BUILD/mod_runtime_test" --real-zip "$BUILD/real-mod" \
  "$ROOT/tests/mods/real/sm64coop-achievements-2.1.0.zip"

# Point this at a private, already-extracted CoopDX mod root to inspect its
# original startup scripts without adding third-party package files to tests/.
if [ -n "${MOD_EXTERNAL_ROOT:-}" ]; then
  "$BUILD/mod_runtime_test" --external-root "$MOD_EXTERNAL_ROOT"
fi

if [ -n "${MOD_EXTERNAL_ZIP:-}" ]; then
  mkdir -p "$BUILD/external-zip"
  "$BUILD/mod_runtime_test" --external-zip "$BUILD/external-zip" "$MOD_EXTERNAL_ZIP"
fi
if [ -n "${MOD_POSSESSION_ZIP:-}" ]; then
  mkdir -p "$BUILD/possession-zip"
  "$BUILD/mod_runtime_test" --possession-zip "$BUILD/possession-zip" "$MOD_POSSESSION_ZIP"
fi
