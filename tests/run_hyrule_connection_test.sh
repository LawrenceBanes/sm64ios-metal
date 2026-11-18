#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-clang}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
cd "$repo_dir"
ulimit -c 0
mkdir "$scratch/build"
gate_flag=

if [ -n "${HYRULE_GENERATED_RENDER_DIR:-}" ]; then
    for f in hyrule_field_connection.h hyrule_field_connected_collision.inc.c hyrule_field_connector.inc.c hyrule_field_spawn.h; do
        cp "$HYRULE_GENERATED_RENDER_DIR/$f" "$scratch/build/$f"
    done
    cp "$HYRULE_GENERATED_RENDER_DIR/hyrule_field_runtime.inc.c" "$scratch/build/hyrule_field_generated.inc.c"
    gate_flag=-DVALIDATE_IMPORTED_GATE
else
    # Synthetic inlet so CI never requires an archive or ROM.
    PYTHONDONTWRITEBYTECODE=1 python3 - "$scratch/build" <<'PY'
from pathlib import Path
import sys
from tools.hyrule_importer import ImportFailure, emit_connected_hyrule

out = Path(sys.argv[1])
vertices = [(-180, 0, -170), (-180, 0, 2200), (180, 0, -170), (180, 0, 2200)]
polygons = [{"vertex_indices": t, "normal": (0, 32767, 0), "surface_type": 0} for t in ((0, 1, 2), (1, 3, 2))]
for collision, scale in ((None, 1.0), ({"vertices": vertices, "polygons": polygons}, 0.5),
                         ({"vertices": vertices, "polygons": []}, 1.0)):
    try:
        emit_connected_hyrule(out, collision, scale)
    except ImportFailure:
        pass
    else:
        raise AssertionError("invalid connection was accepted")
assert not any(out.iterdir())
emit_connected_hyrule(out, {"vertices": vertices, "polygons": polygons}, 1.0)
PY
    cat > "$scratch/build/hyrule_field_spawn.h" <<'HEADER'
#define HYRULE_FIELD_SPAWN_X 0
#define HYRULE_FIELD_SPAWN_Y 0
#define HYRULE_FIELD_SPAWN_Z 1415
#define HYRULE_FIELD_SPAWN_YAW ((s16)-20)
#define HYRULE_FIELD_KART_X -340
#define HYRULE_FIELD_KART_Y 0
#define HYRULE_FIELD_KART_Z 1415
#define HYRULE_FIELD_RETURN_X 960
#define HYRULE_FIELD_RETURN_Y 0
#define HYRULE_FIELD_RETURN_Z 1415
#define HYRULE_FIELD_GATE_X -5891
#define HYRULE_FIELD_GATE_Y -881
#define HYRULE_FIELD_GATE_Z 15676
HEADER
    cat > "$scratch/build/hyrule_field_generated.inc.c" <<'C'
const Gfx hyrule_field_opaque_dl[] = { gsSPEndDisplayList() };
const Gfx hyrule_field_translucent_dl[] = { gsSPEndDisplayList() };
const Collision hyrule_field_collision[] = { COL_INIT(), COL_END() };
C
fi

common="-std=gnu99 -DF3DEX_GBI_2E"
for mode in 000 100 110 111; do
    case "$mode" in
        000) hyrule=0; connected=0; hat=0 ;;
        100) hyrule=1; connected=0; hat=0 ;;
        110) hyrule=1; connected=1; hat=0 ;;
        111) hyrule=1; connected=1; hat=1 ;;
    esac
    make --no-print-directory -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL \
        IOS_CONNECTED_WORLD_TEST=0 IOS_HYRULE_WORLD="$hyrule" IOS_HYRULE_GATE_DIAGNOSTICS="$hyrule" \
        IOS_CONNECTED_WORLD="$connected" IOS_HAT_MOVEMENT="$hat" > "$scratch/make-vars"
    flags=$(awk '/^VERSION_CFLAGS := / { sub(/^VERSION_CFLAGS := /, ""); print; exit }' "$scratch/make-vars")
    test -n "$flags"
    $cc $common $flags -Iinclude -Isrc -I"$scratch" -I. -fsyntax-only \
        levels/castle_grounds/script.c levels/hyrule_field/script.c levels/hyrule_field/leveldata.c \
        src/game/area.c src/game/mario.c src/game/mario_step.c src/game/ios_hat.c src/game/mario_hat.c
    $cc $common $flags -Iinclude -Isrc -I"$scratch" -I. -c levels/castle_grounds/geo.c -o "$scratch/geo.o"
    $cc $common $flags -Iinclude -Isrc -I"$scratch" -I. -E -P levels/castle_grounds/script.c > "$scratch/castle.i"
    $cc $common $flags -Iinclude -Isrc -I"$scratch" -I. -E -P levels/hyrule_field/script.c > "$scratch/field.i"
    grep -q '0x00030000' "$scratch/castle.i" # The stock death warp is always present.
    nm -u "$scratch/geo.o" > "$scratch/geo.syms"
    if [ "$connected" = 1 ]; then
        grep -q hyrule_field_opaque_dl "$scratch/geo.syms"
        grep -q hyrule_field_translucent_dl "$scratch/geo.syms"
        grep -q castle_grounds_hyrule_connector_dl "$scratch/geo.syms"
    else
        ! grep -q hyrule_field_opaque_dl "$scratch/geo.syms" || exit 1
    fi
    if [ "$hyrule:$connected" = 1:0 ]; then
        grep -q '0x28090000' "$scratch/castle.i"
        grep -q '0x28010000' "$scratch/field.i"
    else
        ! grep -q '0x28090000' "$scratch/castle.i" || exit 1
        ! grep -q '0x28010000' "$scratch/field.i" || exit 1
    fi
    printf 'Hyrule %s / connected %s / hat %s: compiled and checked warp isolation\n' "$hyrule" "$connected" "$hat"
done
if make --no-print-directory -pn clean NOEXTRACT=1 IOS_HYRULE_WORLD=0 IOS_CONNECTED_WORLD=1 \
        >"$scratch/invalid" 2>&1; then
    echo 'Connected mode without Hyrule was accepted' >&2
    exit 1
fi
grep -q 'IOS_CONNECTED_WORLD=1 requires TARGET_IOS=1 and IOS_HYRULE_WORLD=1' "$scratch/invalid"

physics_flags="-D_LANGUAGE_C -DVERSION_US -DNON_MATCHING -DAVOID_UB -DIOS_BUILD -DIOS_HYRULE_WORLD -DIOS_CONNECTED_WORLD"
$cc $common $physics_flags -Iinclude -Isrc -I"$scratch" -I. -include sm64.h -include geo_commands.h -x c \
    -fsyntax-only "$scratch/build/hyrule_field_connector.inc.c"
for old_world in 0 1; do
    old_flag=
    if [ "$old_world" = 1 ]; then old_flag=-DIOS_CONNECTED_WORLD_TEST; fi
    $cc $common $physics_flags $old_flag $gate_flag -Iinclude -Isrc -I"$scratch" -I. -ffunction-sections -fdata-sections \
        tests/test_hyrule_connection.c src/engine/surface_collision.c src/engine/surface_load.c src/engine/math_util.c \
        src/game/mario.c src/game/mario_step.c src/game/mario_actions_moving.c \
        -Wl,--gc-sections,--wrap=mario_ios_portal_surface_passable,--wrap=mario_get_terrain_sound_addend -lm -o "$scratch/route"
    "$scratch/route"
done
