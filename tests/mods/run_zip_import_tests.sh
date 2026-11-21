#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM

python3 - "$BUILD" <<'PY'
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo
import sys

root = Path(sys.argv[1])
with ZipFile(root / "lua_actor.zip", "w", ZIP_DEFLATED) as archive:
    archive.writestr("DemoMod/main.lua", "hook_event(HOOK_UPDATE, function() end)\n")
    archive.writestr("DemoMod/actors/Demo/model.bin", b"synthetic model bytes")
    archive.writestr("DemoMod/actors/Demo/model.tex", b"synthetic texture bytes")

with ZipFile(root / "dynos_only.zip", "w", ZIP_DEFLATED) as archive:
    archive.writestr("DemoPack/actors/Demo/model.bin", b"synthetic model bytes")
    archive.writestr("DemoPack/actors/Demo/model.col", b"synthetic collision bytes")

with ZipFile(root / "unrecognized_asset.zip", "w", ZIP_DEFLATED) as archive:
    archive.writestr("UnknownPack/object.col", b"not an upstream ZIP type marker")

with ZipFile(root / "traversal.zip", "w", ZIP_DEFLATED) as archive:
    archive.writestr("../escape.lua", "print('outside')")

with ZipFile(root / "case_collision.zip", "w", ZIP_DEFLATED) as archive:
    archive.writestr("CasePack/main.lua", "return true")
    archive.writestr("CasePack/MAIN.LUA", "return false")

with ZipFile(root / "mixed_mod.zip", "w", ZIP_DEFLATED) as archive:
    archive.writestr("Bowser Moveset/main.lua", "-- name: Bowser Moveset\nreturn true\n")
    archive.writestr("Bowser Moveset/actions.lua", "return {}\n")
    archive.writestr("Bowser Moveset/actors/bowser.bin", b"actor fixture")
    archive.writestr("Template Character/example.lua", "print('not part of the mod')\n")
    archive.writestr("Bowser Moveset/.vs/workspace.db", b"sample metadata")

with ZipFile(root / "multiple_mods.zip", "w", ZIP_DEFLATED) as archive:
    archive.writestr("One/main.lua", "return true\n")
    archive.writestr("Two/main.lua", "return true\n")
PY

cc=${CC:-cc}
"$cc" -std=gnu99 -O1 -g -D_LANGUAGE_C -DF3DEX_GBI_2 -DNON_MATCHING=1 -DAVOID_UB=1 -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT" \
  "$ROOT/tests/mods/zip_import_test.c" "$ROOT/src/pc/fs/fs_packtype_zip.c" "$ROOT/src/pc/fs/dirtree.c" \
  -o "$BUILD/zip_import_test"
"$BUILD/zip_import_test" "$BUILD"
