# iOS Metal GitHub Actions CI

The isolated CI branch adapts the existing default-branch `ios-metal-ipa.yml` workflow path for manual dispatch. GitHub requires a `workflow_dispatch` workflow file to exist on the default branch before a branch-specific run can be requested. This branch has no automatic push or pull-request trigger. It does not modify the default `ios` branch.

The standard GitHub-hosted `xcode-27` arm64 runner currently provides Xcode 27.1 by default and an iPhoneOS 27.1 SDK ([runner image inventory](https://github.com/actions/runner-images/blob/main/images/macos/xcode-27-arm64-Readme.md)). The script accepts Xcode 27.1 or newer within major version 27. It reports the actual Xcode, iPhoneOS SDK, and Metal tools used by each run.

## Asset-free validation

Dispatch `ios-metal-ipa.yml` against the CI branch with `full_game=false`. The runner fetches the pinned SDL2 revision, applies the scene/orientation patches, builds the arm64 iOS static library, compiles the Objective-C Metal backend against device and simulator SDKs, generates representative MSL, and compiles/links it with Apple's Metal tools. The only uploaded artifact is the `safe/` directory containing toolchain, SDL2, Metal validation, and status logs. This stage does not compile the full game or create an IPA.

## Full game build (private inputs required)

For `full_game=true`, both repository variables point to the dedicated private repository. Add one fine-grained Actions secret, `SM64_PRIVATE_BUILD_GH_TOKEN`, scoped only to that repository with Contents read/write permission. Supply `asset_bundle_release_tag` for a draft release containing `sm64-assets.tar.gz` and `sm64-assets.sha256`. Create and transfer that bundle only after explicit asset-transfer approval. The CI script rejects the public source repository, verifies the repository is private, checks the draft release and checksum, and extracts the mapped assets only on the runner. The normal build uses `IOS_HYRULE_WORLD=0` and `IOS_CONNECTED_WORLD_TEST=1`; no `oot.o2r` is required. Supplying the optional `hyrule_o2r_release_tag` enables `IOS_HYRULE_WORLD=1` only after fetching and validating `oot.o2r` and its SHA-256 from the same private repository. For the Mission 61 connected build, use `full_game=true`, `connected_world=true`,
and `hyrule_o2r_release_tag=hyrule-o2r-mission58-20261117`. Set
`hat_movement=true` to test the full combination. The runner also sets
`IOS_CONNECTED_WORLD_TEST=1` and enables gate diagnostics with the Hyrule input.
The approved O2R draft was checked read-only for both asset names and the
archive digest. The runner still checks the draft, SHA-256, ZIP, and scene before
it builds. Ordinary builds keep `IOS_CONNECTED_WORLD=0`.

After the asset-free stage, `ios/build_ios_metal_ci.sh full` compiles the complete unsigned iPhoneOS app with `RENDER_API=METAL`, packages a Payload-style IPA, and runs `ios/validate_ipa.py` with required system-dependency and game-texture checks. It uploads the IPA and checksum only to a uniquely tagged draft release in the configured private repository. The raw game build log and extracted assets stay runner-local. Public Actions artifacts contain bounded diagnostics only. The private release requires authenticated access, and the IPA still needs SideStore signing.

An unsigned IPA is not signed, installed, or verified on an iPhone by this CI. Producing a downloadable game IPA would require a separate, explicitly approved distribution path that accounts for the repository's public visibility and game-asset rights.

## Local checks

From this branch, run:

```bash
bash -n ios/build_ios_metal_ci.sh ios/build_codemagic_connected_world.sh ios/build_sdl2_ios.sh
python3 -m unittest discover -s ios -p 'test_ci_summarize_build_log.py'
CC=clang tests/run_linux_checks.sh
```

The Linux checks do not compile Objective-C against Apple SDKs or validate the full iOS app. The former Codemagic wrapper invokes the same CI entry point, and its configured artifacts are limited to the safe diagnostics directory. It is retained for comparison and is not used for automatic builds.
