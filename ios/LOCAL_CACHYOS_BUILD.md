# Local CachyOS build boundary

## What the repository builds

The native iOS target is an Xcode application project (`sm64ios/sm64ios.xcodeproj`, scheme `sm64ios`), not a generic CMake/Make app. Xcode runs the repository's root `build.sh` shell phase. That phase asks `xcrun` for the `iphoneos` SDK and Apple Clang, sets the arm64/iOS 15 target, invokes the game Makefile with `RENDER_API=METAL`, then copies game resources into the `.app`. The pinned SDL 2.32.10 source is built separately with SDL's Xcode `Static Library-iOS` target by `ios/build_sdl2_ios.sh`.

The iOS application sources contain Objective-C (`.m`) but no Swift source files. The app uses Foundation, UIKit, Metal, MetalKit, QuartzCore, CoreGraphics, CoreAudio, AudioToolbox, AVFoundation, GameController, CoreMotion, CoreHaptics, and CoreBluetooth. Its asset catalog and storyboards must be compiled into iOS bundle resources by Apple's `actool` and `ibtool` through Xcode. The native executable is not enough to form a working app bundle.

`gfx_metal.m` does **runtime MSL compilation**: generated shader text is passed to `-[MTLDevice newLibraryWithSource:options:error:]`, including the presentation shader. There are no checked-in `.metal` sources or precompiled `.metallib` files to build on Linux. The A16 device's Metal implementation compiles these variants at runtime. Linux can test the C shader-source generator, but cannot compile MSL or verify Metal pipelines.

## CachyOS: run all available local checks

Keep the supplied archive and extracted Nintendo assets local and ignored by Git. Run:

```sh
./ios/build_cachyos_local.sh prepare /path/to/oot.o2r
```

This regenerates the Hyrule Field intermediate/native SM64 data under ignored `build/`, temporarily validates the 1,538 USA-mapped extracted game files into a `/tmp` archive and deletes that archive, runs Linux regressions and packaging tests, and builds selected Hyrule/gateway/shared-level C objects with the **Linux host compiler**. It does not invoke GitHub Actions, upload assets, build an iPhoneOS binary, or create a fake IPA.

If a real unsigned iPhoneOS `.app` bundle has been built locally on a Mac and copied to this machine, package and inspect it without Apple tools:

```sh
./ios/build_cachyos_local.sh package /path/to/sm64ios.app /path/to/sm64ios.ipa
```

This creates the `Payload/<app>.app` archive, checks bundle metadata/resources and the arm64 iPhoneOS Mach-O header, and prints the IPA SHA-256. The check does not run Apple's `otool`, verify a signature, install the IPA, or establish that it launches. The existing unit tests use synthetic Mach-O fixtures and validate the packaging code only.

## Why this checkout cannot build the IPA on CachyOS

This host has LLVM/Clang 23, GNU Make 4.4, and LLD, but has no `xcrun`, `xcodebuild`, `actool`, `ibtool`, Apple SDK, `ld64.lld`, or SDL2 iOS static library. A direct iOS-target Objective-C probe stops at missing `Foundation/Foundation.h`. The game Makefile's iOS link requires Apple frameworks and the arm64 iPhoneOS SDK; the Xcode project additionally builds the pinned SDL UIKit backend and compiles its asset catalog/storyboards.

LLVM's target triple and LLD do not supply Apple's UIKit/Metal framework headers, module maps, link stubs, Objective-C runtime environment, SDK build tools, or resource compilers. The project's actual build also calls `xcrun` directly. `osxcross` combines a host compiler with a macOS SDK and Darwin tools; it does not describe an iOS SDK target. The newer [`xcross`](https://github.com/arxdeus/xcross) is specific to Flutter and Kotlin/Compose pipelines and requires a Swift toolchain plus a full `Xcode.xip` input for its Darwin SDK. [`xtool`](https://github.com/xtool-org/xtool) targets Swift Package Manager projects. None consumes this Xcode project or its Make/Xcode shell-phase integration. Replacing the build system to use them would be a port, not a setting change.

More fundamentally, Apple's current Xcode/SDK agreement authorizes execution of Apple software on Apple-branded products running macOS and limits Apple SDK use to development for the corresponding Apple platforms. This plan therefore does not install, extract, or run Apple's SDK/toolchain on CachyOS. See [Apple's Xcode and Apple SDKs Agreement](https://www.apple.com/legal/sla/docs/xcode.pdf).

## Simplest free route to a real sideloadable IPA

Use an existing or borrowed Mac (no paid cloud runner): install Xcode 27 with the iPhoneOS 27 SDK, obtain the pinned SDL 2.32.10 Git checkout, and keep the already-extracted assets and O2R archive local. From the checkout, run:

```sh
./ios/build_local_macos_ipa.sh /path/to/local/SDL2 /path/to/oot.o2r
```

The script verifies Xcode/SDK versions and GNU tools, checks the local extracted assets, copies the pinned SDL revision into a temporary directory before applying the repository's SDL/UIKit patches, builds the `sm64ios` scheme for generic iOS with signing disabled, generates Hyrule C data from the local archive, packages and validates the IPA, and prints its SHA-256. It does not fetch source or assets, call GitHub, sign, or publish. Its Xcode build has not run in this CachyOS environment; the script is syntax-checked here, and the full Mac/iPhoneOS build remains to be run on Apple hardware.

SideStore can sign the unsigned IPA on-device with a free Apple Account. Its current docs describe the free signing period as seven days and the free-account limit as three installed sideloaded apps, so refreshes are needed and the slot limit applies: [SideStore FAQ](https://docs.sidestore.io/docs/faq). No paid Apple Developer membership is required just to sideload for personal testing. A free account does not produce an App Store distribution signature.

If no Mac is available, there is currently no supported, legitimate path in this checkout to a complete local iOS Metal IPA from CachyOS alone. The practical options are to use an Apple Mac you already have access to, or use the existing Apple-hosted workflow (which this task explicitly did not trigger).

## Validation boundary

Passing the CachyOS preflight proves importer behavior, host-side SM64 code compilation, Linux regressions, and IPA packaging/validator behavior. It does not prove iOS SDK compilation, generated MSL compilation, code signing, SideStore installation, or on-device rendering. Those require Xcode/Apple SDK validation and then a physical iPhone test.
