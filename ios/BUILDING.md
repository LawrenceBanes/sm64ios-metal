# iOS build inputs

The native target uses the `sm64ios` Xcode project and the iOS build script selects `RENDER_API=METAL`. SDK and compiler paths are resolved through `xcrun`; the project no longer depends on a fixed `/Applications/Xcode.app` location.

## Required on a macOS build host

- Xcode with iOS SDKs and command line tools (`xcodebuild` and `xcrun`).
- SDL2 iOS headers and static libraries. Set the Xcode build setting `SDL2_ROOT` to the SDL2 checkout; the default is `../../SDL2` relative to the Xcode project. The project reads headers from `SDL2_ROOT/include` and expects `libSDL2.a` plus `libSDLmain.a` in Xcode’s built products. The standalone `build_ios.sh` helper looks for headers and libraries in `SDL2_ROOT/include` and `SDL2_ROOT/lib`. This checkout does not currently contain those SDL2 artifacts or an SDL2 source submodule.
- A legally obtained US ROM image at `baserom.us.z64` for the asset extraction/full-game build. ROM-derived assets are generated locally and are not committed.
- An Apple signing identity and provisioning profile for direct iPhone installation. The CI workflow can instead create an unsigned IPA for subsequent SideStore signing; neither signing nor device installation has been validated here.

The shared scheme is `sm64ios` in `sm64ios/sm64ios.xcodeproj`. Build from Xcode or with `xcodebuild` on macOS after SDL2 and ROM-derived assets are available. The checked-in Xcode shell phase packages the game into the app bundle; `SM64_SKIP_CODESIGN=1` disables its explicit signing calls for unsigned CI output.

For the pinned SDL2 build and private extracted-asset workflow, see [`IOS_IPA_BUILD.md`](IOS_IPA_BUILD.md). The manually dispatched CI job emits an unsigned arm64 IPA and validates its structure; it does not install or launch the app on an iPhone.

## Linux checks

Run `tests/run_linux_checks.sh`. It tests portable shader generation and scene-capture storage, syntax-checks the shared C rendering path with both GL and Metal renderer macros, and builds host asset tools. It does not compile Objective-C Metal code or MSL, produce an iOS app, sign an IPA, or validate rendering on hardware.

For the zero-Actions-cost workflow, including local Hyrule conversion, CachyOS validation, and the exact Apple SDK boundary, see [`LOCAL_CACHYOS_BUILD.md`](LOCAL_CACHYOS_BUILD.md). CachyOS can run `ios/build_cachyos_local.sh prepare <oot.o2r>`; an existing/borrowed Mac can run `ios/build_local_macos_ipa.sh <local-SDL2-checkout> <oot.o2r>` for a real local IPA.
