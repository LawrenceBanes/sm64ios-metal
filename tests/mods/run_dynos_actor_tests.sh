#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM

python3 - "$BUILD" <<'PY'
import struct, sys, zlib
from pathlib import Path
directory = Path(sys.argv[1])
def name(value):
    data = value.encode()
    return bytes([len(data)]) + data
payload = bytearray()
payload += b'\x02' + name('test_texture') + struct.pack('<I', 8) + b'\x89PNG\r\n\x1a\n'
payload += b'\x03' + name('test_vertices') + struct.pack('<I', 3) + b'\0' * 48
payload += b'\x04' + name('test_gfx') + struct.pack('<I', 3)
payload += struct.pack('<II', 0x01000000, 0x52544e50) + name('test_vertices') + struct.pack('<I', 0)
payload += struct.pack('<II', 0x05000000, 0)  # F3DEX2 G_TRI1
payload += struct.pack('<II', 0xdf000000, 0)  # F3DEX2 G_ENDDL
payload += b'\x05' + name('test_geo') + struct.pack('<I', 6)
payload += struct.pack('<III', 0x0b, 0x04, 0x15)
payload += struct.pack('<I', 0x52544e50) + name('test_gfx') + struct.pack('<I', 0)
payload += struct.pack('<II', 0x05, 0x01)
def write(file, body, declared=None):
    (directory / file).write_bytes(b'DYNOSBIN' + struct.pack('<Q', len(body) if declared is None else declared) + zlib.compress(body))
write('synthetic.bin', payload)
direct_root = payload.replace(
    b'\x05' + name('test_geo') + struct.pack('<I', 6) +
    struct.pack('<III', 0x0b, 0x04, 0x15) +
    b'PNTR' + name('test_gfx') + struct.pack('<I', 0) +
    struct.pack('<II', 0x05, 0x01),
    b'\x05' + name('test_geo') + struct.pack('<I', 3) +
    struct.pack('<I', 0x15) +
    b'PNTR' + name('test_gfx') + struct.pack('<I', 0) +
    struct.pack('<I', 0x01),
)
write('direct-root.bin', direct_root)
bad_root_order = payload.replace(
    b'\x05' + name('test_geo') + struct.pack('<I', 6) +
    struct.pack('<III', 0x0b, 0x04, 0x15) +
    b'PNTR' + name('test_gfx') + struct.pack('<I', 0) +
    struct.pack('<II', 0x05, 0x01),
    b'\x05' + name('test_geo') + struct.pack('<I', 4) +
    struct.pack('<IIII', 0x04, 0x0b, 0x05, 0x01),
)
write('bad-root-order.bin', bad_root_order)
offset_payload = payload.replace(
    b'PNTR' + name('test_vertices') + struct.pack('<I', 0),
    b'PNTR' + name('test_vertices') + struct.pack('<I', 1),
).replace(
    b'PNTR' + name('test_gfx') + struct.pack('<I', 0),
    b'PNTR' + name('test_gfx') + struct.pack('<I', 1),
)
write('offsets.bin', offset_payload)
vertex_pointer = b'PNTR' + name('test_vertices') + struct.pack('<I', 0)
write('bad-ref.bin', payload.replace(vertex_pointer,
                                     b'PNTR' + name('gone_vertices') + struct.pack('<I', 0)))
write('cycle.bin', payload.replace(vertex_pointer,
                                   b'PNTR' + name('test_gfx') + struct.pack('<I', 0)))
write('unknown.bin', b'\x0a' + name('unsupported'))
write('oversize.bin', payload, 32 * 1024 * 1024)
(directory / 'truncated.bin').write_bytes((directory / 'synthetic.bin').read_bytes()[:-1])
PY

${CC:-cc} -std=gnu99 -O1 -g ${MOD_TEST_CFLAGS:-} -Wall -Wextra -Werror \
  -D_LANGUAGE_C -DF3DEX_GBI_2 -DNON_MATCHING=1 -DAVOID_UB=1 \
  -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT" \
  "$ROOT/tests/mods/dynos_actor_test.c" "$ROOT/src/pc/mods/dynos_actor.c" \
  "$ROOT/src/pc/mods/dynos_actor_native.c" \
  -o "$BUILD/dynos_actor_test"

"$BUILD/dynos_actor_test" "$BUILD/synthetic.bin"
"$BUILD/dynos_actor_test" "$BUILD/direct-root.bin"
"$BUILD/dynos_actor_test" "$BUILD/bad-root-order.bin"
"$BUILD/dynos_actor_test" "$BUILD/offsets.bin"
"$BUILD/dynos_actor_test" --reject "$BUILD/bad-ref.bin" "$BUILD/cycle.bin" \
  "$BUILD/unknown.bin" "$BUILD/oversize.bin" "$BUILD/truncated.bin"

# The optional private archive is used unchanged as input. Its actor files are
# extracted only into the temporary test directory and are never redistributed.
if [ -n "${MOD_EXTERNAL_ZIP:-}" ]; then
  python3 - "$MOD_EXTERNAL_ZIP" "$BUILD" <<'PY'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    names = [name for name in archive.namelist() if name.endswith('.bin') and '/actors/' in name]
    for index, name in enumerate(names):
        with open(os.path.join(sys.argv[2], f'actor-{index}.bin'), 'wb') as file:
            file.write(archive.read(name))
    print(f'private archive: {len(names)} actor bundles selected')
PY
  for actor in "$BUILD"/actor-*.bin; do
    [ -e "$actor" ] || continue
    "$BUILD/dynos_actor_test" "$actor"
  done
fi
