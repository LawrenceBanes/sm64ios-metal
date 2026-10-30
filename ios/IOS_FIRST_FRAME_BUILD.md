# First native iOS frame: build and asset gates

## Build path audited

The Xcode project has an iOS `sm64ios` application target. Its first build phase runs `build.sh`; for `iphoneos`, that script selects `RENDER_API=METAL`, resolves the Apple compiler and SDK with `xcrun`, then runs `gmake -j4 TARGET_IOS=1`. The Makefile's default `NOEXTRACT=0` invokes `extract_assets.py us` before compiling. The resulting game executable/resources are copied into the app bundle by the build phase.

`extract_assets.py` reads `assets.json`, opens `baserom.us.z64`, verifies its SHA-1 against `sm64.us.sha1`, and extracts the mapped graphics, animation/demo, and audio inputs. The user supplied a legally obtained USA ROM at `/home/larry/Downloads/Super Mario 64 (USA).z64`. Its checksum validation passed and extraction produced 3,650 files on the Linux development machine. The staged `baserom.us.z64`, extracted assets, and `.assets-local.txt` are gitignored and remain local; ROMs and extracted Nintendo assets must never be committed or included in public CI artifacts.

The normal build defaults to `EXTERNAL_DATA=0`, so extracted content is compiled into generated game resources. `EXTERNAL_DATA=1` instead puts the generated sound/texture resources in the app bundle's `res` data pack; it still requires extracted assets. Neither mode removes the need for the ROM during the first build.

## Other build dependencies and signing

The Xcode target references `libSDL2.a` and `libSDLmain.a` in `BUILT_PRODUCTS_DIR`, while its build settings expect SDL2 headers under the sibling `SDL2` directory. No SDL2 checkout/static libraries are present in this repository. Homebrew's host SDL2 package is not a substitute for libraries built for iOS ARM64. The CI build must build or fetch a pinned SDL2 iOS static-library product, stage those archives in `BUILT_PRODUCTS_DIR`, and pass the matching headers through `SDL2_ROOT`/`SDL2_INCLUDE_DIR`.

`build.sh` also signed the app directly with `CODE_SIGN_IDENTITY`. It now accepts `SM64_SKIP_CODESIGN=1` to omit both manual codesign calls during an unsigned compile/package pass. Xcode signing or SideStore re-signing still requires appropriate signing material. The repository contains no signing certificate, provisioning profile, or device UDID, and none should be committed.

## Protected asset build design

The macOS build must be a manually dispatched job in the private `LawrenceBanes/sm64ios-metal` repository. A hosted runner cannot see the ROM or extracted files that exist only on the Linux development machine. A practical transfer is a short-lived download URL to an owner-controlled private object store, saved as an Actions secret (proposed name: `BASEROM_US_Z64_URL`). The workflow can download it silently to the runner's temporary worktree, let `extract_assets.py` perform its built-in SHA-1 check and extraction, build with `RENDER_API=METAL`, and clean the ROM plus extracted source assets in an `always()` step. No such URL or equivalent private transfer is configured yet.

The artifact should contain only the `.ipa` (or unsigned app package) and a small, sanitized build summary, have a one-day retention, and remain in this private repository. An IPA necessarily contains its game resources, so it must never be uploaded to a public workflow artifact, release, or public repository. Signing credentials, if used, belong in encrypted Actions secrets/environment variables and a temporary keychain; never in build logs or Git history.

The development checkout now has the ignored ROM and extracted files, but no private object-store URL or signing material is configured for hosted CI. The complete app workflow cannot run remotely yet. I have not created a workflow that silently falls back to public asset hosting or publishes game resources.

## Runtime evidence required

The Metal backend now logs shader-generation/compilation failures with shader IDs, pipeline failures with state keys, texture allocation failures, and GPU command-buffer errors. It logs completion of the first submitted Metal command buffer, which is useful diagnostics but does not prove that a frame was visibly presented. The existing texture loader logs each missing external texture path.

To confirm the first real frame, install the signed app on the iPhone 14 Pro Max and capture both a device screenshot showing the SM64 scene and the corresponding device console interval. Then validate a textured level, touch movement/jump, a Bluetooth controller, audio, background/resume, and save persistence. CI compilation and simulator output cannot substitute for that physical-device evidence.
