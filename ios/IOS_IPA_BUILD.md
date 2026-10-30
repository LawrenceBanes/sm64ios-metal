# Native arm64 iOS IPA build

`.github/workflows/ios-metal-ipa.yml` is the full, manual-only iPhoneOS build. It runs the Metal `sm64ios` Xcode target on the Xcode 27 arm64 GitHub runner, builds SDL2 from a pinned upstream source revision, uses the already-extracted USA assets, creates an unsigned IPA, and validates its layout and Mach-O architecture. The ROM-independent Apple compiler workflow remains separate.

## Pinned SDL2 dependency

The workflow fetches SDL2 commit `5d249570393f7a37e037abf22cd6012a4cc56a71` (SDL 2.32.10) and verifies the checkout before building. `ios/build_sdl2_ios.sh` builds its iOS `SDLmain-iOS` and `Static Library-iOS` targets for arm64, checks both archives with `lipo`, stages `libSDL2.a` and `libSDLmain.a` into Xcode's product directory, and copies headers into the required `include/SDL2` layout. The iOS link places `-lSDLmain` before `-lSDL2`, uses Apple's `libc++`, and omits desktop-only `-no-pie`, `-lpthread`, `libstdc++`, and IOKit flags. No SDL binary is committed.

The pinned source release is available from [SDL's upstream release page](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10). SDL documents its Xcode-based iOS builds in [README-ios](https://github.com/libsdl-org/SDL/blob/release-2.32.10/docs/README-ios.md).

## Private asset handoff

The ROM stays in the developer checkout. Create an archive from the 1,538 USA-mapped extracted files only:

```sh
python3 ios/asset_bundle.py create /tmp/sm64-assets.tar.gz
```

Upload that archive to an owner-controlled private object store. Configure two repository Actions secrets in the private `LawrenceBanes/sm64ios-metal` repository:

- `SM64_ASSET_BUNDLE_URL`: an HTTPS download URL with enough lifetime for the manually dispatched build.
- `SM64_ASSET_BUNDLE_SHA256`: SHA-256 of the archive.

The archive is checked against `assets.json`, limited to the USA-mapped regular asset files, and extracted into the temporary runner checkout. The build uses `NOEXTRACT=1` and never needs the ROM. An `always()` cleanup removes extracted files and temporary SDL/build data. Neither archive nor ROM is uploaded as an Actions artifact or committed.

The produced IPA contains the compiled game assets, so the IPA artifact is available only from this private repository and retained for one day. Workflow logs contain compiler output but not archive contents or the download URL. Do not mirror the IPA or asset bundle to a public release or artifact store.

## IPA packaging and checks

`ios/package_unsigned_ipa.py` creates `Payload/<app>.app` and zips it as an unsigned `.ipa`; it does not invoke signing and preserves executable modes. It is portable, so Linux tests exercise the actual packaging routine. `ios/validate_ipa.py` checks the ZIP structure, `Info.plist`, iPhone support, landscape metadata, compiled touch-control `Assets.car`, an arm64 iPhoneOS main executable, arm64 embedded frameworks, absence of provisioning/signature files, and system or embedded dynamic dependencies via `otool`.

The Linux unit tests exercise IPA parsing with synthetic Mach-O headers; that is only a validator test, not a build or install result. A CI IPA passing these checks still needs SideStore signing and a real iPhone install/launch before runtime success can be claimed.

## Current intervention needed

GitHub DNS is failing in the development shell, and `gh auth status` reports the stored credential invalid. No credential reset or login was attempted. After DNS returns, recheck authentication and push the local commits. Before dispatching this workflow, configure the two private asset secrets above. The workflow refuses to build without them.
