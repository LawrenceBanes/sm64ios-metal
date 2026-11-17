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

## Hat movement test build

Set the Xcode build setting `IOS_HAT_MOVEMENT=1`, or run
`IOS_HAT_MOVEMENT=1 bash build_ios.sh` on the configured Mac. The default is
`0`. This option works with either Hyrule setting.

- Tap **HAT** below the on-screen **B**, or press the controller's top face
  button (SDL **Y**, Xbox **Y**, PlayStation **Triangle**). A custom mapping
  that already uses SDL **Y** keeps its existing action.
- Steer to aim, or release the stick to throw along Mario's facing or backward
  travel direction. Throw from the ground or during a normal jump with the
  regular cap equipped.
- Hold the normal jump button and steer so Mario's feet descend onto the hat.
  Contact gives one bounce per airtime and preserves horizontal velocity. The
  hat returns on its own after a bounce or a short wait.

Run `sh tests/run_ios_hat_test.sh` for the host gameplay checks and the
hat/Hyrule compiler matrix.
