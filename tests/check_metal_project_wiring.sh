#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pbxproj="$repo_dir/sm64ios/sm64ios.xcodeproj/project.pbxproj"
makefile="$repo_dir/Makefile"

for source in gfx_metal.m gfx_metal_shader.c gfx_metal_state.c gfx_scene_capture.c; do
    grep -Fq "$source" "$pbxproj"
done
grep -Fq 'MetalKit.framework' "$pbxproj"
grep -Fq 'Metal.framework' "$pbxproj"
grep -Fq 'src/pc/gfx' "$makefile"
grep -Fq -- '-framework Metal' "$makefile"
grep -Fq -- '-framework MetalKit' "$makefile"
grep -Fq 'filter GL GL_LEGACY' "$makefile"
echo "Metal Xcode references and Makefile source/framework wiring checks passed"
