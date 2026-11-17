# Mission 57.25C — GitHub Actions iOS Metal CI

## Result

**Partial:** the manual, asset-free Apple-toolchain job passed on the public repository. The full SM64 app and unsigned IPA have not been built by this workflow because the required private asset source is not configured. No game asset, ROM, IPA, or credential was uploaded by this run.

- CI branch: `ci/ios-metal-m57-25c` (isolated from `ios` and Mission 57 engine files)
- Prepared CI commit integrated into this branch: `6b1f3d7b` (same change as `64a66e2`)
- Tested CI commit: `b9a54b566429727769d255b6bfd42dd7fcb47ac2`
- Manual run: [44123471753](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/44123471753)
- Run input: `full_game=false`; no push or pull-request trigger on this branch
- Runner: standard GitHub-hosted `xcode-27` arm64
- Actual toolchain: Xcode 27.1 (27A9275), iPhoneOS SDK 27.1, Apple Metal toolchain 27.1.266.1
- Safe artifact: `ios-metal-safe-diagnostics-44123471753` (toolchain, SDL2, Metal validation, status logs only)

## Verified

1. SDL2 revision `5d249570393f7a37e037abf22cd6012a4cc56a71` built as an iPhoneOS arm64 static library with the project's scene/orientation patches. Xcode reported `BUILD SUCCEEDED`.
2. The Objective-C Metal backend compiled and linked against both iPhoneOS arm64 and iOS Simulator arm64 SDKs.
3. Ten generated MSL shader variants compiled and linked into Metal libraries, including textured, fogged, dual-texture, alpha, and enhanced-presentation cases.
4. Metal Xcode project and Makefile wiring checks passed.
5. `CC=clang tests/run_linux_checks.sh`, shell syntax, Python summary tests, YAML parsing, and `actionlint` passed locally. `actionlint` required a local ignore for its stale `xcode-27` runner-label list; the runner label itself was verified by the successful hosted run.
6. GitHub reported the full-game step as **skipped**, not passed. The safe diagnostic artifact was downloaded and inspected: four log/status files, no app or IPA.

## Asset and release safeguards

The source repository is public. The prior workflow's same-repository draft-release asset input and public IPA release/upload path were removed from this branch. The replacement full build accepts only a separate private repository, a read-only token, a draft asset bundle, and a verified checksum. It refuses to use `LawrenceBanes/sm64ios-metal` as an asset repository. Only the bounded, asset-free `safe/` diagnostics directory is uploaded. The raw full-build log, extracted assets, app, and IPA stay runner-local and are removed at job exit. Fork pull requests have no workflow trigger and cannot invoke the private-input step via this branch's configuration.

No Actions secrets or variables are currently configured in this repository. A full iOS compilation and IPA validation therefore requires an independently reviewed private asset-transfer setup, including `SM64_ASSET_PRIVATE_REPOSITORY`, `SM64_ASSET_GH_TOKEN`, and an `asset_bundle_release_tag`. The current test did not attempt that step. The optional Hyrule archive also remains unconfigured and untested.

The public repository already contains older IPA releases from previous workflows. This mission neither downloaded nor changed those releases; they need a separate ownership and distribution review.

## Scope and limits

No gameplay, physics, collision, Metal renderer behavior, or Mission 57 engine source changed. The branch changed CI workflows, reusable build scripts, safety tests, and build documentation. The current direct iOS backend and MSL compile result does **not** establish that the complete game links, packages, installs, or renders on an iPhone. The unsigned IPA path is implemented and locally syntax-tested but awaits the private inputs and a full macOS run.

The branch remains unmerged. Do not merge into `ios` without separate review: its parent includes the Mission 57 engine commits in addition to the CI work.
