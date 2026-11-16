# Mission 55.5 report — GitHub Actions iOS Build Recovery

**Status:** PREPARED; remote run not started
**Starting local HEAD:** `94f7da885a94593e934cc8748f81209464256c55`
**Remote `ios` head verified through GitHub read-only API:** `c456f2fb5ea5d4400cdc3d851c29e140a0cfc2c2`
**Run URL:** N/A — awaiting push, Actions-usage review, and user approval
**Artifact:** none

## Workflow prepared

- **Name:** Mission 55.5 Connected World iOS Build
- **File:** `.github/workflows/mission-55-5-connected-world.yml`
- **Trigger:** `workflow_dispatch` only; no push, pull-request, or schedule trigger.
- **Runner:** `xcode-27`; the workflow verifies installed Xcode 27.x, iPhoneOS SDK 27.x, `metal`, and `metallib` before building. The actual runner/Xcode version is not known until a run starts.
- **Build:** full `sm64ios` iPhoneOS arm64 Xcode target, `IOS_CONNECTED_WORLD_TEST=1`, `IOS_HYRULE_WORLD=0`, code signing disabled. It uses pinned SDL2 `5d249570393f7a37e037abf22cd6012a4cc56a71` and the existing private extracted USA-asset draft. No O2R archive is needed for the synthetic Mission 55.4 branch.
- **Metal validation:** compiles and links the Objective-C backend against device and simulator SDKs, generates representative MSL variants, and compiles/links AIR and metallib outputs with Apple's tools.
- **Packaging:** only runs after app compilation succeeds; packages and validates the unsigned IPA. No release is created. The one-day artifact contains the app/IPA, checksum, toolchain record, and build logs in the private repository.
- **Permissions/retries:** repository contents read-only; no automatic retries, no signing credentials, no workflow release write, and no paid runner-size override.

## Audit results

The local branch is `ios`; remotes are `metal-origin` (the writable project repository) and `origin` (the upstream). The GitHub read-only API reports remote `ios` at `c456f2fb`, while local HEAD is `94f7da88` and contains nine descendants of that remote commit. **The latest Mission 55.4 commits are not pushed**, so dispatching now would build stale source.

Local push/CLI access is currently blocked. `gh auth status` reports the cached local token invalid, while its API request fails DNS resolution for `api.github.com`. SSH push fails before connecting because `/etc/ssh/ssh_config.d/20-systemd-ssh-proxy.conf` has unacceptable ownership/permissions; HTTPS remote lookup also cannot resolve GitHub. No credentials were replaced or displayed. The GitHub read-only connector can inspect the repository but does not provide a route to push this local commit history.

The latest Actions records are:

- [`41363939026` — Native iOS Metal IPA](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/41363939026): Linux job failed; iOS job skipped.
- [`41363939019` — Metal Apple SDK validation](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/41363939019): Linux and Apple jobs failed.

Both runs used commit `c456f2fb`. Their job records show `runner_id=0`, empty runner names, and **zero steps**; downloaded logs are no longer available (artifact-log endpoint returned `BlobNotFound`). Therefore these failures happened before source compilation and do not identify a compiler/linker failure. Earlier runs `41358059303` and `41358059222` on commit `b384a3f9` completed successfully, including the iOS arm64 IPA build and Apple Metal validation, establishing that the `xcode-27` label worked at that time.

The existing private draft `sm64-assets-20261031-04` is present and contains `sm64-assets.tar.gz` (9,952,431 bytes) plus its checksum. The workflow verifies draft metadata, checksum, and mapped archive contents before extraction. The original archive and ROM-derived files are not uploaded as workflow artifacts.

## Results and blockers

- **Workflow syntax and contract:** PASS locally. YAML parsed; static test verifies manual-only trigger, private repository guard, read-only permissions, connected-only flags, signing disabled, and artifact-only output.
- **Host validation:** PASS. `CC=clang tests/run_linux_checks.sh` passed (31 importer tests plus the Linux checks); gateway tests passed 8/8, partition tests 3/3, and IPA/checksum tests 16/16. Existing warning: tautological comparison in `mario_actions_airborne.c:75`.
- **Native iOS compile:** NOT RUN.
- **Apple Metal/Msl compile:** NOT RUN.
- **Unsigned IPA packaging:** NOT RUN.
- **Artifact and run URL:** NONE; no Actions job was dispatched.
- **Billing/quota availability:** UNVERIFIED. Local `gh` cannot reach GitHub with the current DNS/token state; the read-only connector rejects the account-specific Actions usage endpoint; no browser is available. No billing settings were changed.
- **iPhone gameplay:** UNVERIFIED and requires later installation/testing; an unsigned IPA is not directly installable.

## Approval checkpoint

The workflow and local checks are ready, but two gates remain before any hosted build:

1. Restore local GitHub connectivity/authentication and re-check the remote before pushing. The current local `ios` branch is nine commits ahead of remote `ios`; do not force-push or overwrite a changed remote.
2. Push the work to a new `mission-55-5-connected-world` branch after confirming it is still unused. This branch name was absent in the latest read-only branch search. Pushing to `ios` would also trigger the existing `Metal Apple SDK validation` workflow, so the separate branch avoids an unintended hosted run.
3. Review Actions quota/billing in GitHub account settings and explicitly approve one manual `xcode-27` run. The workflow caps the job at 60 minutes; its actual charge or free-quota impact cannot be verified from this session.

No run should be started until all gates are satisfied. The prior failure records establish that no steps ran, but their expired logs do not reveal why GitHub failed to allocate runners. After approval and push, record the exact workflow run URL, runner/Xcode versions, compile and MSL results, IPA validation, and artifact availability here.
