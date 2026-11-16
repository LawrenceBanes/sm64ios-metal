# Mission 55.5 report — GitHub Actions iOS Build Recovery

**Status:** PREPARED; remote push and run blocked by missing OAuth scopes
**Starting local HEAD:** `94f7da885a94593e934cc8748f81209464256c55`
**Remote `ios` head verified through GitHub read-only API:** `c456f2fb5ea5d4400cdc3d851c29e140a0cfc2c2`
**Run URL:** N/A — awaiting push and Actions-usage review
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

The normal sandbox cannot resolve GitHub or access the host resolver. A read-only connectivity check outside the sandbox reached `api.github.com` (HTTP 200), and the existing `gh` keyring login for `LawrenceBanes` works there. The cached token has `repo` but lacks `workflow` and `user` scopes. SSH fails on `/etc/ssh/ssh_config.d/20-systemd-ssh-proxy.conf`; bypassing that file still gives `Permission denied (publickey)`. HTTPS Git with the existing `gh` credential helper can read remote `ios`.

An HTTPS push to the new `mission-55-5-connected-world` branch was rejected by GitHub because creating `.github/workflows/mission-55-5-connected-world.yml` requires the OAuth `workflow` scope. No remote ref was created. A `gh auth refresh -s workflow,user` attempt prompted for browser authorization and timed out; subsequent `gh auth status` showed unchanged scopes. No token was replaced or exposed, and no system security setting was changed.

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
- **Repository Actions:** ENABLED (`allowed_actions=all`) according to the repository API; the repository is private. GitHub documents `xcode-27` as a standard hosted runner for private repositories, but an actual runner assignment remains unverified.
- **Billing/quota availability:** UNVERIFIED. The personal billing usage API returned HTTP 404 with a specific `user` scope requirement. No billing settings were changed.
- **iPhone gameplay:** UNVERIFIED and requires later installation/testing; an unsigned IPA is not directly installable.

## Approval checkpoint

The user approved one hosted run only after access and allowance checks. The workflow and local checks are ready, but the required gates remain:

1. Complete GitHub's browser authorization for `gh auth refresh -h github.com -s workflow,user`, or supply an existing credential with those scopes. Recheck scopes afterward. The temporary device code from the timed-out attempt should not be reused.
2. Recheck that the remote branch is still unused, then push the local work to `mission-55-5-connected-world` without force. Pushing to `ios` would also trigger the existing `Metal Apple SDK validation` workflow.
3. Verify that the personal Actions allowance covers one 60-minute `xcode-27` job. If the API still cannot provide it, inspect GitHub's billing dashboard directly. A runner label being available does not prove a remaining allowance.

No run should be started until all gates are satisfied. The prior failure records establish that no steps ran, but their expired logs do not reveal why GitHub failed to allocate runners. After the push and allowance check, record the exact workflow run URL, runner/Xcode versions, compile and MSL results, IPA validation, and artifact availability here. Do not dispatch a second run without new approval.
