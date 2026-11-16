# Mission 55.6 — Codemagic build handoff

## Prepared configuration

The root `codemagic.yaml` defines one manual workflow, `connected-world-ios`, on
the standard `mac_mini_m2` machine with Xcode 27.1 and a 60-minute limit. It
omits `triggering` and `publishing`. The workflow calls
`ios/build_codemagic_connected_world.sh`; it does not change the GitHub Actions
workflows or the stock game build.

The script verifies the iPhoneOS SDK and Metal tools, builds the pinned SDL2
revision with the existing iOS scene patch, compiles the Metal backend and
representative generated MSL, and builds the full iPhoneOS arm64 app with
`IOS_CONNECTED_WORLD_TEST=1`, `IOS_HYRULE_WORLD=0`, and signing disabled. It
then packages and validates an unsigned IPA. The Hyrule flag remains available
in the project; this workflow selects the synthetic same-area forest prototype
and therefore requires no OoT archive.

## Private asset prerequisite

Codemagic's GitHub checkout does not provide a GitHub API token to the build
script. In the Codemagic app for `LawrenceBanes/sm64ios-metal`, create an
**app-level, Secret** environment variable named `SM64_ASSET_GH_TOKEN` in a
group named `sm64_private_assets`. Use a GitHub credential limited to read
access to this private repository and its draft release assets. The script
uses it only to read the existing `sm64-assets-20261031-04` draft, verifies
metadata and SHA-256, extracts mapped USA files, then unsets the token before
the compiler runs. Do not store this token, the ROM, or extracted assets in Git.

The script removes temporary downloads and extracted build inputs on exit.
The compiled app and IPA intentionally contain game resources and must stay
within a private Codemagic app with restricted artifact access. Codemagic
artifacts include the unsigned IPA, `.app`, checksum, toolchain record, Metal
validation output, and compiler logs. No release, email, or publishing step is
configured.

## Manual run gate

1. Confirm Codemagic has access to the private GitHub repository and select
   the dedicated `mission-55-6-codemagic` branch for configuration scanning.
2. Confirm the account is on the individual free plan, the M2 machine is
   available, and at least 60 free M2 minutes remain. Do not enable billing or
   choose an M4 or other paid instance.
3. Confirm the secret group exists and the draft release is accessible from
   the Codemagic build identity.
4. Start exactly one `connected-world-ios` build manually only after explicit
   authorization. Do not configure webhooks or automatic triggers.

The YAML and shell syntax can be checked on Linux. Xcode, iPhoneOS, Metal
compiler, signing, IPA contents, and device behavior remain unverified until a
Codemagic run and later iPhone test. An unsigned IPA needs signing before
installation.
