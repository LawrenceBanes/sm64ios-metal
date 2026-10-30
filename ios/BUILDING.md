# iOS build inputs

The native target uses the `sm64ios` Xcode project and the iOS build script selects `RENDER_API=METAL`. SDK and compiler paths are resolved through `xcrun`; the project no longer depends on a fixed `/Applications/Xcode.app` location.

## Required on a macOS build host

- Xcode with iOS SDKs and command line tools (`xcodebuild` and `xcrun`).
- SDL2 iOS headers and static libraries. Set the Xcode build setting `SDL2_ROOT` to the SDL2 checkout; the default is `../../SDL2` relative to the Xcode project. The project reads headers from `SDL2_ROOT/include` and expects `libSDL2.a` plus `libSDLmain.a` in Xcode’s built products. The standalone `build_ios.sh` helper looks for headers and libraries in `SDL2_ROOT/include` and `SDL2_ROOT/lib`. This checkout does not currently contain those SDL2 artifacts or an SDL2 source submodule.
- A legally obtained US ROM image at `baserom.us.z64` for the asset extraction/full-game build. ROM-derived assets are generated locally and are not committed.
- An Apple signing identity and provisioning profile to install on an iPhone. SideStore installation also requires a compatible signed IPA; signing and device installation have not been validated here.

The shared scheme is `sm64ios` in `sm64ios/sm64ios.xcodeproj`. Build from Xcode or with `xcodebuild` on macOS after SDL2 and ROM inputs are configured. The checked-in Xcode shell phase currently performs packaging and signing as part of the target build.

## Linux checks

Run `tests/run_linux_checks.sh`. It tests portable shader generation and scene-capture storage, syntax-checks the shared C rendering path with both GL and Metal renderer macros, and builds host asset tools. It does not compile Objective-C Metal code or MSL, produce an iOS app, sign an IPA, or validate rendering on hardware.
