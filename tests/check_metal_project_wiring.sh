#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pbxproj="$repo_dir/sm64ios/sm64ios.xcodeproj/project.pbxproj"
makefile="$repo_dir/Makefile"

for source in gfx_metal.m gfx_metal_shader.c gfx_shader64_pack.c gfx_metal_state.c gfx_scene_capture.c; do
    grep -Fq "$source" "$pbxproj"
done
grep -Fq 'MetalKit.framework' "$pbxproj"
grep -Fq 'Metal.framework' "$pbxproj"
grep -Fq 'CoreGraphics' "$repo_dir/ios/validate-metal-apple.sh"
grep -Fq 'metal_backend_diagnostics_stub.m' "$repo_dir/ios/validate-metal-apple.sh"
grep -Fq 'shader64_wrap_v2.c' "$repo_dir/ios/validate-metal-apple.sh"
grep -Fq '#if !TARGET_OS_SIMULATOR' "$repo_dir/src/pc/gfx/gfx_metal.m"
for shader_id in 0x01200200u 0x00000045u 0x00000200u 0x01200a00u; do
    grep -Fq "$shader_id" "$repo_dir/tests/metal_shader_emit.c"
done
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
printf '%s\n' "$ios_ldflags" | grep -Fq -- '-framework OpenGLES'
ios_cpp=$(make -s -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL IOSLIBS=/tmp/ios-products | sed -n 's/^CPP := //p' | head -n 1)
test "$ios_cpp" = "clang -E -P -x c"
ios_as=$(make -s -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL IOSLIBS=/tmp/ios-products CC=clang | sed -n 's/^AS := //p' | head -n 1)
test "$ios_as" = "clang -target x86_64-unknown-linux-gnu"
ios_objcopy=$(IOS_OBJCOPY=/opt/homebrew/bin/gobjcopy make -s -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL IOSLIBS=/tmp/ios-products CC=clang | sed -n 's/^OBJCOPY := //p' | head -n 1)
test "$ios_objcopy" = "/opt/homebrew/bin/gobjcopy"
test "$(grep -F -c -- '-x assembler -c -o $@ $<' Makefile)" -eq 2
ios_asflags=$(make -s -pn clean NOEXTRACT=1 TARGET_IOS=1 TARGET_OS_IOS=1 RENDER_API=METAL IOSLIBS=/tmp/ios-products CC=clang | sed -n 's/^ASFLAGS := //p' | head -n 1)
printf '%s\n' "$ios_asflags" | grep -Fq -- '-Wa,--defsym,AVOID_UB=1'
printf '%s\n' "$ios_asflags" | grep -Fq -- '-Wa,--defsym,F3DEX_GBI_2E=1'
grep -Fq -- '$(AS) $(ASFLAGS) -x assembler -c -o $@ $<' "$makefile"
if printf '%s\n' "$ios_ldflags" | grep -Fq -- '-lSDLmain'; then
    echo "pinned SDL2 iOS archive includes SDL's UIKit entry point; no separate SDLmain library is available" >&2
    exit 1
fi
grep -Fq -- '-target "Static Library-iOS"' "$repo_dir/ios/build_sdl2_ios.sh"
grep -Fq 'SDL_uikit_main' "$repo_dir/ios/build_sdl2_ios.sh"
echo "Metal Xcode references and Makefile source/framework wiring checks passed"

# Included C fragments depend on their owning unit and must never be compiled alone.
python3 - <<'PYTHON'
import subprocess
result = subprocess.run(
    ["make", "-s", "-pn", "clean", "NOEXTRACT=1", "TARGET_IOS=1",
     "TARGET_OS_IOS=1", "RENDER_API=METAL", "IOS_HYRULE_WORLD=1",
     "IOS_CONNECTED_WORLD=1", "IOS_HAT_MOVEMENT=1"],
    check=True, capture_output=True, text=True,
)
line = next(line for line in result.stdout.splitlines() if line.startswith("C_FILES := "))
sources = line.split(" := ", 1)[1].split()
assert "src/game/mario.c" in sources
assert "src/game/mario_superslide.c" in sources
assert not any(path.endswith(".inc.c") for path in sources), "Make compiled an include fragment"
print("iOS Make source selection includes gameplay units and excludes C fragments")
PYTHON
