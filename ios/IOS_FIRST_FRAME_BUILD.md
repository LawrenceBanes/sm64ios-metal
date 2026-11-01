# First native iOS frame: build and asset gates

## Build path audited

The Xcode project has an iOS `sm64ios` application target. Its first build phase runs `build.sh`; for `iphoneos`, that script selects `RENDER_API=METAL`, resolves the Apple compiler and SDK with `xcrun`, then runs `gmake -j4 TARGET_IOS=1`. The Makefile's default `NOEXTRACT=0` invokes `extract_assets.py us` before compiling. The resulting game executable/resources are copied into the app bundle by the build phase.

`extract_assets.py` reads `assets.json`, opens `baserom.us.z64`, verifies its SHA-1 against `sm64.us.sha1`, and extracts the mapped graphics, animation/demo, and audio inputs. The user supplied a legally obtained USA ROM at `/home/larry/Downloads/Super Mario 64 (USA).z64`. Its checksum validation passed. The USA build uses 1,538 files from the asset map; 502 additional entries are for other regional versions and are not required in the USA handoff. The staged `baserom.us.z64`, extracted assets, and `.assets-local.txt` are gitignored and remain local; ROMs and extracted Nintendo assets must never be committed or included in public CI artifacts.

The normal Makefile default is `EXTERNAL_DATA=0`, so extracted content is compiled into generated game resources. The iOS IPA workflow explicitly builds with `EXTERNAL_DATA=1`: that preserves canonical original texture paths, puts original textures in the app bundle's `res/base.zip`, and enables optional user-installed HD overrides with an original fallback. It still requires extracted assets. Neither mode removes the need for the ROM during the first build.

## Other build dependencies and signing

The Xcode target references `libSDL2.a` and `libSDLmain.a` in `BUILT_PRODUCTS_DIR`, while its build settings expect SDL2 headers under the sibling `SDL2` directory. No SDL2 checkout/static libraries are present in this repository. Homebrew's host SDL2 package is not a substitute for libraries built for iOS ARM64. The CI build must build or fetch a pinned SDL2 iOS static-library product, stage those archives in `BUILT_PRODUCTS_DIR`, and pass the matching headers through `SDL2_ROOT`/`SDL2_INCLUDE_DIR`.

`build.sh` also signed the app directly with `CODE_SIGN_IDENTITY`. It now accepts `SM64_SKIP_CODESIGN=1` to omit both manual codesign calls during an unsigned compile/package pass. Xcode signing or SideStore re-signing still requires appropriate signing material. The repository contains no signing certificate, provisioning profile, or device UDID, and none should be committed.

## Protected asset build

The manual-only full IPA workflow and its private extracted-asset bundle procedure are documented in [`IOS_IPA_BUILD.md`](IOS_IPA_BUILD.md). The bundle contains only the USA-mapped extracted files, is hash checked and path validated on the runner, then cleaned. No ROM is sent to GitHub.

## Runtime evidence required

The Metal backend now logs shader-generation/compilation failures with shader IDs, pipeline failures with state keys, texture allocation failures, and GPU command-buffer errors. It logs completion of the first submitted Metal command buffer, which is useful diagnostics but does not prove that a frame was visibly presented. The existing texture loader logs each missing external texture path.

To confirm the first real frame, install the signed app on the iPhone 14 Pro Max and capture both a device screenshot showing the SM64 scene and the corresponding device console interval. Then validate a textured level, touch movement/jump, a Bluetooth controller, audio, background/resume, and save persistence. CI compilation and simulator output cannot substitute for that physical-device evidence.
