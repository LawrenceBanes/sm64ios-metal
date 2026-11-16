# Local-first iOS build strategy

## Verified CachyOS capability

This host is Linux x86_64. It has Clang and `ld64.lld`, but does not have `xcodebuild`, `xcrun`, Swift, `actool`, `ibtool`, Apple's `metal`/`metallib`, `codesign`, `ldid`, or an iPhoneOS SDK. The project has an Xcode project and workspace, but their presence does not make them buildable on this host. `ld64.lld` cannot replace the UIKit, Metal, Foundation, asset-catalog, SDK, and signing components supplied by Apple's toolchain.

The current Metal backend generates MSL at runtime and compiles shader variants through Metal's runtime library APIs; the Xcode project also builds the Objective-C/Objective-C++ backend. Linux can validate the source and host-side shader generation, but cannot run Apple's MSL compiler or prove that iOS SDK symbols and availability checks link.

## Commands available locally

From the repository root:

```sh
# Full no-cost host regression suite
CC=clang tests/run_linux_checks.sh

# Re-import the supplied private O2R and run the complete local preflight
./ios/build_cachyos_local.sh prepare /home/larry/Downloads/oot.o2r

# Focused region conversion, if only one intermediate is needed
python3 tools/hyrule_importer.py /home/larry/Downloads/oot.o2r build/hyrule_field_import
python3 tools/hyrule_importer.py /home/larry/Downloads/oot.o2r build/kokiri_forest_import \
  --scene-prefix scenes/shared/spot04_scene

# Host syntax check of generated Kokiri SM64 C
clang -fsyntax-only -std=gnu99 -D_LANGUAGE_C -DF3DEX_GBI_2 \
  -DNON_MATCHING=1 -DAVOID_UB=1 -I include -I src -I . \
  build/kokiri_forest_import/render/kokiri_forest_runtime.inc.c

# Package an already-built unsigned .app only (does not compile or sign it)
./ios/build_cachyos_local.sh package /path/to/sm64ios.app /path/to/sm64ios.ipa
```

`prepare` verifies the local extracted SM64 assets, converts Hyrule and the Kokiri default-scene subset into ignored build outputs, runs Linux regressions and packaging fixtures, and compiles selected Hyrule/gateway C objects for the Linux host. It explicitly does not produce an IPA.

## Apple build path

The repository's local macOS script is the no-hosted-CI route:

```sh
./ios/build_local_macos_ipa.sh /path/to/local/SDL /path/to/oot.o2r \
  build/local-iphoneos/sm64ios-arm64-unsigned.ipa
```

It requires macOS with Xcode 27.x and iPhoneOS SDK 27.x, plus GNU Make, `gobjcopy`, Python, the local extracted USA SM64 asset set, the supplied private `oot.o2r`, and an SDL Git checkout containing the exact pinned revision `5d249570393f7a37e037abf22cd6012a4cc56a71`. The script builds SDL2 for arm64, applies the repository's UIKit scene/orientation patches in a temporary worktree, runs `xcodebuild` with code signing disabled, packages the `.app` as an unsigned IPA, and validates package contents. It does not sign or install the IPA.

No local macOS/Xcode environment was found on this CachyOS host, so this path was not executed. No hosted workflow was dispatched; there is no paid Actions spend from this mission. The simplest genuinely free build route is a Mac already available to Larry with Xcode installed, using the script above. If no such Mac is available, a compatible Apple-hosted build runner or another Mac is still required for the actual iOS target; Linux cross-linking without the Apple SDK is not a validated substitute.

## Asset privacy

Keep `oot.o2r`, the SM64 ROM, extracted game assets, and generated Nintendo data local and ignored. The local scripts use temporary bundles for asset checks and remove them after validation. Do not place asset archives in a public GitHub release, Actions cache, public artifact, or Git commit. A future remote build needs an authenticated private transfer and checksum verification; that is outside this local-only strategy.

## What the local checks establish

Linux tests and host compilation establish parser behavior, generated C syntax, build-flag separation, source-level collision grid configuration, and platform-independent gameplay logic. They do not compile Objective-C against iOS headers, compile MSL with Apple's compiler, link UIKit/Metal, produce a signed app, or establish visible rendering or gameplay on iPhone.
