#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-clang}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
cd "$repo_dir"

# This fixture supplies the generated positions to the level scripts without
# requiring a private O2R archive in the normal Linux checks.
mkdir "$scratch/build"
cat > "$scratch/build/hyrule_field_spawn.h" <<'HEADER'
#define HYRULE_FIELD_SPAWN_X 160
#define HYRULE_FIELD_SPAWN_Y 0
#define HYRULE_FIELD_SPAWN_Z 1415
#define HYRULE_FIELD_SPAWN_YAW ((s16)-20)
#define HYRULE_FIELD_KART_X -340
#define HYRULE_FIELD_KART_Y 0
#define HYRULE_FIELD_KART_Z 1415
#define HYRULE_FIELD_RETURN_X 960
#define HYRULE_FIELD_RETURN_Y 0
#define HYRULE_FIELD_RETURN_Z 1415
HEADER

common="-std=gnu99 -D_LANGUAGE_C -DVERSION_US -DF3DEX_GBI_2 -DNON_MATCHING=1 -DAVOID_UB=1 -DIOS_BUILD=1"
engine_sources="src/game/ios_hat.c src/game/mario_hat.c src/game/mario_step.c src/game/area.c src/game/level_update.c"
sdl_flags=$(pkg-config --cflags sdl2)

for hyrule in off on; do
    world=
    if [ "$hyrule" = on ]; then world=-DIOS_HYRULE_WORLD; fi
    for hat in off on; do
        feature=
        if [ "$hat" = on ]; then feature=-DIOS_HAT_MOVEMENT; fi
        $cc $common $world $feature $sdl_flags -DCAPI_SDL2 -fsyntax-only \
            -Iinclude -Isrc -I"$scratch" -I. $engine_sources src/game/mario.c \
            src/pc/controller/controller_sdl2.c levels/hyrule_field/script.c
        printf 'Hat %s / Hyrule %s: C syntax checks passed\n' "$hat" "$hyrule"
    done

    $cc $common $world -DIOS_HAT_MOVEMENT -ffunction-sections -fdata-sections \
        -Iinclude -Isrc -I. tests/test_ios_hat.c $engine_sources \
        src/game/mario_actions_airborne.c src/engine/math_util.c \
        -Wl,--gc-sections -lm -o "$scratch/hat-test"
    "$scratch/hat-test"
done
