#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/sm64-world-scene.XXXXXX")
off_obj=$(mktemp "${TMPDIR:-/tmp}/sm64-world-scene-off.XXXXXX.o")
on_obj=$(mktemp "${TMPDIR:-/tmp}/sm64-world-scene-on.XXXXXX.o")
trap 'rm -f "$test_bin" "$off_obj" "$on_obj"' EXIT HUP INT TERM
"${CC:-clang}" -std=gnu99 -D_LANGUAGE_C -I"$repo_dir/include" -I"$repo_dir/src" \
    -I"$repo_dir" -c "$repo_dir/src/engine/world_scene.c" -o "$off_obj"
if nm -g "$off_obj" | grep -q 'world_scene_open'; then
    echo 'scene runtime symbols leaked into the feature-disabled object' >&2
    exit 1
fi
"${CC:-clang}" -std=gnu99 -D_LANGUAGE_C -DIOS_O2R_SCENE_RUNTIME=1 \
    -I"$repo_dir/include" -I"$repo_dir/src" -I"$repo_dir" \
    -c "$repo_dir/src/engine/world_scene.c" -o "$on_obj"
nm -g "$on_obj" | grep -q 'world_scene_open'
off_flags=$(make -s -pn -C "$repo_dir" NOEXTRACT=1 IOS_O2R_SCENE_RUNTIME=0 2>/dev/null \
    | awk '/^VERSION_CFLAGS :=/ { print; exit }')
on_flags=$(make -s -pn -C "$repo_dir" NOEXTRACT=1 IOS_O2R_SCENE_RUNTIME=1 2>/dev/null \
    | awk '/^VERSION_CFLAGS :=/ { print; exit }')
case "$off_flags" in *-DIOS_O2R_SCENE_RUNTIME*) echo 'scene runtime flag enabled by default' >&2; exit 1 ;; esac
case "$on_flags" in *-DIOS_O2R_SCENE_RUNTIME*) ;; *) echo 'scene runtime flag missing from Makefile compile flags' >&2; exit 1 ;; esac
if make -s -pn -C "$repo_dir" NOEXTRACT=1 IOS_O2R_SCENE_RUNTIME=2 >/dev/null 2>&1; then
    echo 'invalid scene runtime flag was accepted' >&2
    exit 1
fi
"${CC:-clang}" -std=gnu99 -Wall -Wextra -Werror -D_LANGUAGE_C \
    -DIOS_O2R_SCENE_RUNTIME=1 \
    -I"$repo_dir/include" -I"$repo_dir/src" -I"$repo_dir" \
    "$repo_dir/tests/test_world_scene.c" "$repo_dir/src/engine/world_scene.c" \
    -o "$test_bin"
"$test_bin"
