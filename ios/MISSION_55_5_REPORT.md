# Mission 55.5 report — GitHub Actions iOS Build Recovery

**Status:** dedicated branch pushed; manual build held for allowance verification
**Starting local HEAD:** `94f7da885a94593e934cc8748f81209464256c55`
**Remote `ios` head verified through GitHub read-only API:** `c456f2fb5ea5d4400cdc3d851c29e140a0cfc2c2`
**Manual run URL:** N/A — no manual build dispatched
**Artifact:** none

## Latest result (2026-11-15)

- The refreshed `LawrenceBanes` CLI login has `repo`, `workflow`, and `user` scopes. The private repository reports Actions enabled with all actions allowed. Remote `ios` remains `c456f2fb`; `mission-55-5-connected-world` was created at `acefdbea` without force.
- GitHub unexpectedly recorded a failed **push** event at [run 43805415136](https://github.com/LawrenceBanes/sm64ios-metal/actions/runs/43805415136). It has zero jobs and no logs; no runner was allocated. Local `actionlint` found the cause: `${{ runner.temp }}` is unavailable in a job-level `env` expression. The workflow now uses `${{ github.workspace }}` for that path. `actionlint` passes after ignoring its outdated warning that the documented `xcode-27` label is unknown; the local workflow contract test also passes.
- GitHub Free includes $12 of monthly Actions compute. The November billing summary reports $14.854 of gross Actions compute ($8.976 Linux, $0.002 Linux slim, $5.876 macOS), fully discounted so far. The API does not state a remaining allowance or explain the extra discount, so a further 60-minute macOS job (up to $3.72 at the published rate) is **not confirmed covered**. No manual run was dispatched.

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

The initial HTTPS push to `mission-55-5-connected-world` was rejected because creating the workflow required OAuth `workflow` scope. A scope-refresh attempt initially timed out. The user then completed the refresh, and the branch push succeeded as described above. No token was displayed or system security setting changed.

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
- **Billing/quota availability:** usage summary retrieved after scope refresh; remaining allowance for a 60-minute macOS job is UNVERIFIED. No billing settings were changed.
- **iPhone gameplay:** UNVERIFIED and requires later installation/testing; an unsigned IPA is not directly installable.

## Approval checkpoint

The user approved one hosted run only if the remaining allowance covers it. Verify that coverage in GitHub's billing dashboard or another authoritative account source before dispatch. The workflow is capped at 60 minutes, but the usage summary alone cannot prove the next run is free. Do not start a manual run or retry without that verification. Keep the corrected workflow on the dedicated branch and leave `ios` untouched.
