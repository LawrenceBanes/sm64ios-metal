# Native arm64 iOS IPA build

The current CI entry point and private-asset requirements are documented in [GitHub Actions CI](GITHUB_ACTIONS_CI.md). This file intentionally contains no command that uploads extracted assets or a game IPA to the public source repository.

The pinned SDL2 dependency is commit `5d249570393f7a37e037abf22cd6012a4cc56a71` (SDL 2.32.10). `ios/build_sdl2_ios.sh` applies the project's UIKit scene/orientation patches, builds SDL's iOS static library for arm64, verifies the UIKit entry point and architecture, and stages headers. The full game build additionally needs GNU Make 4+, GNU binutils `gobjcopy`, Xcode 27.1+ with an iPhoneOS 27 SDK, and a private bundle of locally extracted USA game assets.

The reusable `ios/build_ios_metal_ci.sh` supports two modes:

- `validate`: build SDL2, compile the Objective-C Metal backend for iPhoneOS and simulator, generate MSL shader variants, and compile them with Apple's Metal tools. No game assets are needed.
- `full`: after `validate` in the same output directory, retrieve a checksummed bundle from a separate authenticated private repository, compile the full Metal app without code signing, package `Payload/sm64ios.app` as an unsigned IPA, and run the structural/architecture/resource validator. Optional Hyrule input is also private and opt-in.

The IPA and raw full-game build log stay on the runner and are removed when the job ends. Only asset-free logs and a bounded build summary are uploaded. This CI does not create a downloadable IPA or establish on-device gameplay. Distribution of an asset-bearing build requires a separate decision and process.

The host-side IPA tests use synthetic fixtures. They verify packaging and validator behavior, not Apple compilation or iPhone installation.
