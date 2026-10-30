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
grep -Fq -- '-lSDL2 -lc++' "$makefile"
if grep -Fq -- '-lstdc++' "$makefile"; then
    echo "legacy libstdc++ link flag must not be used for iOS" >&2
    exit 1
fi
ios_ldflags=$(make -s -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL IOSLIBS=/tmp/ios-products | sed -n 's/^LDFLAGS := //p' | head -n 1)
case "$ios_ldflags" in
    *-no-pie*|*-lpthread*|*-framework\ IOKit*)
        echo "iOS link flags contain unsupported desktop options: $ios_ldflags" >&2
        exit 1
        ;;
esac
printf '%s\n' "$ios_ldflags" | grep -Fq -- '-lSDL2 -lc++'
ios_cpp=$(make -s -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL IOSLIBS=/tmp/ios-products | sed -n 's/^CPP := //p' | head -n 1)
test "$ios_cpp" = "cpp -P"
if printf '%s\n' "$ios_ldflags" | grep -Fq -- '-lSDLmain'; then
    echo "pinned SDL2 iOS archive includes SDL's UIKit entry point; no separate SDLmain library is available" >&2
    exit 1
fi
grep -Fq -- '-target "Static Library-iOS"' "$repo_dir/ios/build_sdl2_ios.sh"
grep -Fq 'SDL_uikit_main' "$repo_dir/ios/build_sdl2_ios.sh"
echo "Metal Xcode references and Makefile source/framework wiring checks passed"
