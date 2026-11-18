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
#define HYRULE_FIELD_GATE_X -5891
#define HYRULE_FIELD_GATE_Y -881
#define HYRULE_FIELD_GATE_Z 15676
HEADER

common="-std=gnu99 -DF3DEX_GBI_2"
engine_sources="src/game/ios_hat.c src/game/mario_hat.c src/game/mario_step.c src/game/area.c src/game/level_update.c"
sdl_flags=$(pkg-config --cflags sdl2)

check_hat_symbol() {
    if nm -g "$scratch/$2.o" | awk -v symbol="$3" '$NF == symbol { found = 1 } END { exit !found }'; then
        actual=1
    else
        actual=0
    fi
    if [ "$actual" != "$1" ]; then
        printf '%s.o: unexpected %s presence for IOS_HAT_MOVEMENT=%s\n' "$2" "$3" "$1" >&2
        return 1
    fi
}

for hyrule in 0 1; do
    for hat in 0 1; do
        make --no-print-directory -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL \
            IOS_HYRULE_WORLD="$hyrule" IOS_HYRULE_GATE_DIAGNOSTICS="$hyrule" IOS_HAT_MOVEMENT="$hat" \
            > "$scratch/make-vars"
        flags=$(awk '/^VERSION_CFLAGS := / { sub(/^VERSION_CFLAGS := /, ""); print; exit }' "$scratch/make-vars")
        test -n "$flags"
        for unit in ios_hat mario_hat mario mario_step area level_update; do
            $cc $common $flags -Iinclude -Isrc -I"$scratch" -I. -c "src/game/$unit.c" -o "$scratch/$unit.o"
        done
        $cc $common $flags $sdl_flags -DCAPI_SDL2 -fsyntax-only \
            -Iinclude -Isrc -I"$scratch" -I. src/pc/controller/controller_sdl2.c levels/hyrule_field/script.c
        check_hat_symbol "$hat" ios_hat ios_hat_bounce
        check_hat_symbol "$hat" mario_hat mario_hat_execute_throw
        check_hat_symbol "$hat" mario_hat mario_hat_try_bounce
        check_hat_symbol "$hat" mario mario_hat_update
        check_hat_symbol "$hat" mario mario_hat_execute_throw
        check_hat_symbol "$hat" mario_step mario_hat_try_bounce
        check_hat_symbol "$hat" mario_step mario_hat_on_landing
        check_hat_symbol "$hat" area mario_hat_reset
        check_hat_symbol "$hat" level_update mario_hat_reset
        printf 'Hat %s / Hyrule %s: Make flags and compiled symbols verified\n' "$hat" "$hyrule"
    done

    # The second matrix entry has the hat enabled. Link its portable gameplay
    # code against the test services for collision and recovery checks.
    $cc $common $flags -ffunction-sections -fdata-sections \
        -Iinclude -Isrc -I. tests/test_ios_hat.c $engine_sources \
        src/game/mario_actions_airborne.c src/pc/cheats.c src/engine/math_util.c \
        -Wl,--gc-sections -lm -o "$scratch/hat-test"
    "$scratch/hat-test"
done
