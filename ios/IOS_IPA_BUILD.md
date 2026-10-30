# Native arm64 iOS IPA build

`.github/workflows/ios-metal-ipa.yml` is the full, manual-only iPhoneOS build. It runs the Metal `sm64ios` Xcode target on the Xcode 27 arm64 GitHub runner, builds SDL2 from a pinned upstream source revision, uses the already-extracted USA assets, creates an unsigned IPA, and validates its layout and Mach-O architecture. The ROM-independent Apple compiler workflow remains separate.

## Pinned SDL2 dependency

The workflow installs GNU Make 4+ because the game Makefile uses its shell-assignment syntax, plus GNU binutils for extracting sound-sequence data, then fetches SDL2 commit `5d249570393f7a37e037abf22cd6012a4cc56a71` (SDL 2.32.10) and verifies the checkout before building. Data-only `.s` sound sequences are assembled as temporary x86_64 ELF objects and converted to raw `.m64` blobs with the absolute Homebrew `gobjcopy` path passed into the Xcode script phase; they are not linked into the arm64 iOS executable. `ios/build_sdl2_ios.sh` builds SDL's `Static Library-iOS` target for arm64, checks the archive contains SDL's UIKit entry point and has the right architecture, stages `libSDL2.a`, and copies upstream headers into `include/SDL2`. SDL 2.32.10 has no separate `SDLmain-iOS` target; its static library includes `SDL_uikit_main.c`. Xcode 27 currently supports iOS deployment targets from 15.0 upward, so the app and SDL library use iOS 15.0 as their minimum. The iOS link uses Apple's `libc++` and omits desktop-only `-no-pie`, `-lpthread`, `libstdc++`, and IOKit flags. No SDL binary is committed.

The pinned source release is available from [SDL's upstream release page](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10). SDL documents its Xcode-based iOS builds in [README-ios](https://github.com/libsdl-org/SDL/blob/release-2.32.10/docs/README-ios.md).

## Private asset handoff

The ROM stays in the developer checkout. Create an archive from the 1,538 USA-mapped extracted files only:

```sh
python3 ios/asset_bundle.py create /tmp/sm64-assets.tar.gz
```

For the simplest one-time transfer, use a temporary **draft release in the private project repository**. It requires no extra hosting account or URL secret and is deleted, along with its temporary tag, by the IPA workflow even when a later build step fails:

```sh
bundle=/tmp/sm64-assets.tar.gz
python3 ios/asset_bundle.py create "$bundle"
sha256sum "$bundle" | awk '{print $1}' > /tmp/sm64-assets.sha256
tag="sm64-assets-$(date -u +%Y%m%dT%H%M%SZ)-$(head -c 4 /dev/urandom | od -An -tx1 | tr -d ' \n')"
gh release create "$tag" "$bundle" /tmp/sm64-assets.sha256 \
  --repo LawrenceBanes/sm64ios-metal \
  --draft \
  --title 'Temporary private asset bundle' \
  --notes 'Temporary private asset bundle for sm64ios-metal IPA build.'
gh workflow run ios-metal-ipa.yml \
  --repo LawrenceBanes/sm64ios-metal \
  --ref ios \
  -f "asset_bundle_release_tag=$tag"
```

The manual workflow downloads the two files using its repository-scoped token, validates the SHA-256 and archive contents, and removes the draft release at job cleanup. Draft releases do not create a Git tag ref. This places the asset bundle briefly in GitHub's private release storage; it is not published and is not attached to the IPA artifact. Confirm the repository remains private. If you prefer an owner-controlled private object store, the workflow also accepts the existing `SM64_ASSET_BUNDLE_URL` and `SM64_ASSET_BUNDLE_SHA256` Actions secrets instead.

The archive is limited to the USA-mapped regular asset files and checked against `assets.json`, then extracted into the temporary runner checkout. The build uses `NOEXTRACT=1` and never needs the ROM. An `always()` cleanup removes extracted files and temporary SDL/build data. Neither archive nor ROM is uploaded as an Actions artifact or committed.

The produced IPA contains the compiled game assets, so the IPA artifact is available only from this private repository and retained for one day. Workflow logs contain compiler output but not archive contents or the download URL. Do not mirror the IPA or asset bundle to a public release or artifact store.

## IPA packaging and checks

`ios/package_unsigned_ipa.py` creates `Payload/<app>.app` and zips it as an unsigned `.ipa`; it does not invoke signing and preserves executable modes. It is portable, so Linux tests exercise the actual packaging routine. `ios/validate_ipa.py` checks the ZIP structure, `Info.plist`, iPhone support, landscape metadata, compiled touch-control `Assets.car`, an arm64 iPhoneOS main executable, arm64 embedded frameworks, absence of provisioning/signature files, and system or embedded dynamic dependencies via `otool`.

The Linux unit tests exercise IPA parsing with synthetic Mach-O headers; that is only a validator test, not a build or install result. A CI IPA passing these checks still needs SideStore signing and a real iPhone install/launch before runtime success can be claimed.

## Current intervention needed

The DNS failure was isolated to the restricted development shell: a host-level connectivity check resolved GitHub and reached HTTPS. GitHub CLI authentication was revalidated and the two pending commits were pushed to `metal-origin/ios`. The local USA asset bundle contains 1,538 files and is 9.5 MiB compressed. A temporary private-release option was added so the first build does not depend on configuring an external object-store URL or long-lived repository secrets.
