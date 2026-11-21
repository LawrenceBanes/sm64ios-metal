#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM

LUA_SRC="$ROOT/vendor/lua53/src"
cp -R "$ROOT/tests/mods/fixtures" "$BUILD/fixtures"
python3 - "$BUILD/fixtures/mods/00_core/actors" \
  "${MOD_TEST_DYNOS_ACTOR_ZIP:-}" "${MOD_TEST_DYNOS_ACTOR_ENTRY:-}" \
  "${MOD_TEST_DYNOS_ACTOR_NAME:-}" <<'PY'
import struct, sys, zlib
from pathlib import Path
import zipfile
root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=True)
def name(value):
    raw = value.encode(); return bytes([len(raw)]) + raw
body = bytearray()
body += b'\x03' + name('fixture_vertices') + struct.pack('<I', 3) + bytes(48)
body += b'\x04' + name('fixture_dl') + struct.pack('<I', 3)
body += struct.pack('<II', 0x01000000, 0x52544e50) + name('fixture_vertices') + struct.pack('<I', 0)
body += struct.pack('<II', 0x05000000, 0) + struct.pack('<II', 0xdf000000, 0)
body += b'\x05' + name('fixture_actor') + struct.pack('<I', 6)
body += struct.pack('<III', 0x0b, 0x04, 0x00)
body += struct.pack('<I', 0x52544e50) + name('fixture_branch') + struct.pack('<I', 0)
body += struct.pack('<II', 0x05, 0x01)
body += b'\x05' + name('fixture_branch') + struct.pack('<I', 5)
body += struct.pack('<II', 0x04, 0x15)
body += struct.pack('<I', 0x52544e50) + name('fixture_dl') + struct.pack('<I', 0)
body += struct.pack('<II', 0x05, 0x01)
compressed = zlib.compress(body)
(root / 'fixture_actor.bin').write_bytes(b'DYNOSBIN' + struct.pack('<Q', len(body)) + compressed)
archive_path, member_name, model_name = sys.argv[2:5]
if archive_path or member_name or model_name:
    if not archive_path or not member_name or not model_name:
        raise SystemExit('set MOD_TEST_DYNOS_ACTOR_ZIP, _ENTRY, and _NAME together')
    if not model_name.replace('_', '').replace('-', '').isalnum():
        raise SystemExit('invalid model name for temporary actor fixture')
    with zipfile.ZipFile(archive_path) as archive:
        info = archive.getinfo(member_name)
        if info.file_size <= 0 or info.file_size > 8 * 1024 * 1024:
            raise SystemExit('actor bundle has invalid or oversized length')
        (root / f'{model_name}.bin').write_bytes(archive.read(info))
    print(f'private DynOS actor fixture: {member_name} ({info.file_size} bytes)')
PY
# MOD_TEST_CFLAGS is intentionally split into compiler arguments so this same
# host fixture can be run with sanitizers without changing its build command.
${CC:-cc} -std=gnu99 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections ${MOD_TEST_CFLAGS:-} -D_LANGUAGE_C -DF3DEX_GBI_2 -DMOD_RUNTIME_HOST_TEST -DMOD_RUNTIME_REAL_ZIP \
  -DNON_MATCHING=1 -DAVOID_UB=1 \
  -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT" -I"$LUA_SRC" \
  "$ROOT/tests/mods/host_runtime_test.c" \
  "$ROOT/src/pc/mods/mod_runtime.c" "$ROOT/src/pc/mods/mod_audio.c" "$ROOT/src/pc/mods/mod_resource.c" \
  "$ROOT/src/pc/mods/dynos_actor.c" "$ROOT/src/pc/mods/dynos_actor_native.c" \
  "$ROOT/src/engine/geo_layout.c" "$ROOT/src/engine/graph_node.c" \
  "$ROOT/src/engine/graph_node_manager.c" "$ROOT/src/game/rendering_graph_node.c" \
  "$ROOT/src/pc/fs/fs_packtype_zip.c" "$ROOT/src/pc/fs/dirtree.c" \
  "$LUA_SRC/lapi.c" "$LUA_SRC/lcode.c" "$LUA_SRC/lctype.c" "$LUA_SRC/ldebug.c" \
  "$LUA_SRC/ldo.c" "$LUA_SRC/ldump.c" "$LUA_SRC/lfunc.c" "$LUA_SRC/lgc.c" \
  "$LUA_SRC/llex.c" "$LUA_SRC/lmem.c" "$LUA_SRC/lobject.c" "$LUA_SRC/lopcodes.c" \
  "$LUA_SRC/lparser.c" "$LUA_SRC/lstate.c" "$LUA_SRC/lstring.c" "$LUA_SRC/ltable.c" \
  "$LUA_SRC/ltm.c" "$LUA_SRC/lundump.c" "$LUA_SRC/lvm.c" "$LUA_SRC/lzio.c" \
  "$LUA_SRC/lauxlib.c" "$LUA_SRC/lbaselib.c" "$LUA_SRC/lcorolib.c" \
  "$LUA_SRC/lmathlib.c" "$LUA_SRC/lstrlib.c" "$LUA_SRC/ltablib.c" "$LUA_SRC/lutf8lib.c" \
  -lm -o "$BUILD/mod_runtime_test"

MOD_TEST_DYNOS_GRAPH_RENDER=1 "$BUILD/mod_runtime_test" "$BUILD/fixtures"
printf '%s  %s\n' \
  7001744cb4023be31460960a0e4ff060f4311f573ccdac880564deb532617a3d \
  "$ROOT/tests/mods/real/sm64coop-achievements-2.1.0.zip" | sha256sum -c -
mkdir -p "$BUILD/real-mod"
"$BUILD/mod_runtime_test" --real-zip "$BUILD/real-mod" \
  "$ROOT/tests/mods/real/sm64coop-achievements-2.1.0.zip"

# Exercise a CoopDX-style Mario action callback through the production ZIP
# importer and Lua runtime. The source fixture mirrors the original
# play_mario_sound(m, CHAR_SOUND_*, CHAR_SOUND_*) signature without packaging
# or modifying any community archive.
python3 - "$BUILD/mario-sound-fixture.zip" "$BUILD/fixtures/external_mario_sound/main.lua" <<'PY'
import sys, zipfile
archive, script = sys.argv[1:]
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as z:
    z.write(script, "main.lua")
PY
mkdir -p "$BUILD/mario-sound-install"
(
  unset MOD_TEST_EXPECT_OMM_MODELS MOD_TEST_OMM_EXTRA_MODELS
  unset MOD_TEST_OMM_ANIMATION MOD_TEST_OMM_ACTIONS MOD_TEST_REQUIRE_TEXTURE_HUD
  MOD_TEST_DISPATCH_SET_ACTION=1 "$BUILD/mod_runtime_test" --external-zip \
    "$BUILD/mario-sound-install" "$BUILD/mario-sound-fixture.zip"
)

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
