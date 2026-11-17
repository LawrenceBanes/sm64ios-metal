# iOS Metal GitHub Actions CI

The isolated CI branch adapts the existing default-branch `ios-metal-ipa.yml` workflow path for manual dispatch. GitHub requires a `workflow_dispatch` workflow file to exist on the default branch before a branch-specific run can be requested. This branch has no automatic push or pull-request trigger. It does not modify the default `ios` branch.

The standard GitHub-hosted `xcode-27` arm64 runner currently provides Xcode 27.1 by default and an iPhoneOS 27.1 SDK ([runner image inventory](https://github.com/actions/runner-images/blob/main/images/macos/xcode-27-arm64-Readme.md)). The script accepts Xcode 27.1 or newer within major version 27. It reports the actual Xcode, iPhoneOS SDK, and Metal tools used by each run.

## Asset-free validation

Dispatch `ios-metal-ipa.yml` against the CI branch with `full_game=false`. The runner fetches the pinned SDL2 revision, applies the scene/orientation patches, builds the arm64 iOS static library, compiles the Objective-C Metal backend against device and simulator SDKs, generates representative MSL, and compiles/links it with Apple's Metal tools. The only uploaded artifact is the `safe/` directory containing toolchain, SDL2, Metal validation, and status logs. This stage does not compile the full game or create an IPA.

## Full game build (private inputs required)

For `full_game=true`, set the repository variable `SM64_ASSET_PRIVATE_REPOSITORY` to a **separate private** `owner/repo`, set the Actions secret `SM64_ASSET_GH_TOKEN` to a token with read-only access to that private repository, and supply `asset_bundle_release_tag` for a draft release there containing `sm64-assets.tar.gz` and `sm64-assets.sha256`. Create and transfer that bundle only through an independently reviewed private process. The CI script rejects the public source repository as an asset source, verifies the repository is private, checks the draft release and checksum, and extracts the mapped assets only on the runner. The normal build uses `IOS_HYRULE_WORLD=0` and `IOS_CONNECTED_WORLD_TEST=1`; no `oot.o2r` is required. Supplying the optional `hyrule_o2r_release_tag` enables `IOS_HYRULE_WORLD=1` only after fetching and validating `oot.o2r` and its SHA-256 from the same private repository. That combined mode remains unvalidated until it is actually built.

After the asset-free stage, `ios/build_ios_metal_ci.sh full` compiles the complete unsigned iPhoneOS app with `RENDER_API=METAL`, packages a Payload-style IPA, and runs `ios/validate_ipa.py` with required system-dependency and game-texture checks. The IPA and raw game build log remain runner-local. They are removed at job exit and are **not** uploaded to an Actions artifact or GitHub release. A bounded summary gives compiler error file basenames, line numbers, and error categories without publishing source excerpts. The checksum in the safe summary proves only which IPA was validated during that run; it is not a downloadable build.

An unsigned IPA is not signed, installed, or verified on an iPhone by this CI. Producing a downloadable game IPA would require a separate, explicitly approved distribution path that accounts for the repository's public visibility and game-asset rights.

## Local checks

From this branch, run:

```bash
bash -n ios/build_ios_metal_ci.sh ios/build_codemagic_connected_world.sh ios/build_sdl2_ios.sh
python3 -m unittest discover -s ios -p 'test_ci_summarize_build_log.py'
CC=clang tests/run_linux_checks.sh
```

The Linux checks do not compile Objective-C against Apple SDKs or validate the full iOS app. The former Codemagic wrapper invokes the same CI entry point, and its configured artifacts are limited to the safe diagnostics directory. It is retained for comparison and is not used for automatic builds.
