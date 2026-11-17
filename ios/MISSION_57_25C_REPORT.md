# Mission 57.25C — GitHub Actions iOS Metal CI

## Result

**Partial:** the manual, asset-free Apple-toolchain job passed on the public repository. The full SM64 app and unsigned IPA have not been built because local assets have not been approved for transfer to the private build vault. No game asset, ROM, IPA, or credential has been uploaded.

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

The source repository is public. The prior workflow's same-repository asset path and public IPA upload path were removed. An empty private repository, `LawrenceBanes/sm64ios-private-builds`, has been created for the asset bundle and private IPA draft release; no assets or binaries have been uploaded. The public source repository variables `SM64_ASSET_PRIVATE_REPOSITORY` and `SM64_IPA_PRIVATE_REPOSITORY` now point to it. The full build requires one fine-grained Actions secret, `SM64_PRIVATE_BUILD_GH_TOKEN`, with Contents read/write permission scoped only to the private vault. It refuses to use the public source repo as either private destination, verifies privacy, and uploads the validated IPA and checksum only to a uniquely tagged draft release in the private vault. Only bounded asset-free diagnostics go to public Actions artifacts. Raw logs and extracted assets stay on the runner. This branch has no fork pull-request trigger.

The two repository variables are prepared; the fine-grained secret and private draft asset bundle are still missing. After Larry explicitly approves asset transfer, create a draft release in the private vault with `sm64-assets.tar.gz` and `sm64-assets.sha256`, add `SM64_PRIVATE_BUILD_GH_TOKEN`, then manually run `ios-metal-ipa.yml` on `ci/ios-metal-m57-25c` with `full_game=true` and that release tag. The build will use `IOS_CONNECTED_WORLD_TEST=1`, `IOS_HYRULE_WORLD=0`; no O2R archive is required. The optional Hyrule archive remains unconfigured and untested.

The public repository already contains older IPA releases from previous workflows. This mission neither downloaded nor changed those releases; they need a separate ownership and distribution review.

## Scope and limits

No gameplay, physics, collision, Metal renderer behavior, or Mission 57 engine source changed. The branch changed CI workflows, reusable build scripts, safety tests, and build documentation. The current direct iOS backend and MSL compile result does **not** establish that the complete game links, packages, installs, or renders on an iPhone. The unsigned IPA path is implemented and locally syntax-tested but awaits the private inputs and a full macOS run.

The branch remains unmerged. Do not merge into `ios` without separate review: its parent includes the Mission 57 engine commits in addition to the CI work. The private IPA upload path is newly prepared and has not been exercised yet.
