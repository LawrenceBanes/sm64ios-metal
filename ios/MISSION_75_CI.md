# Mission 75 — Reliable Apple CI

## Build loop

The `Native iOS Metal IPA` workflow accepts pushes to `ios`, `ci/ios-metal-m57-25c`, and `mission-*` branches. Pushes create a workflow record, but the Linux and Apple jobs are skipped unless the pushed head commit message contains `[build-ipa]`. This keeps intermediate development commits from allocating runners. A full build requires the existing `workflow_dispatch` `full_game=true` input or the push marker.

On push builds, append `[scene-diagnostic]` to the same commit message to compile the Documents-based WSC scene loader and diagnostic menu. Manual dispatch continues to use the existing `scene_diagnostic`, `connected_world`, `hat_movement`, asset-release, and O2R-release inputs. For the Mission 74 Hyrule test use `[build-ipa] [scene-diagnostic]`; the default push configuration keeps connected-world stitching, hat movement, and Hyrule O2R disabled.

The concurrency group remains per ref with `cancel-in-progress: false`. A marked commit is not retried automatically after a failed run.

## Private vault API handling

The full build reads the authenticated core API quota through `GET /rate_limit` before private asset access. It stops before downloads if fewer than 12 core requests remain. Asset and O2R API calls are wrapped once and fail fast on rate-limit responses; there are no automatic retries against a primary reset or secondary limit. Diagnostics distinguish a rate-limit 403/429 from an ordinary permission 403 and never print the token. The private repository visibility check is reused at upload time instead of querying the repository twice.

The existing `SM64_PRIVATE_BUILD_GH_TOKEN` remains the fallback and needs `Contents: read and write` on the private vault. A second PAT does **not** isolate the user's primary API quota: authenticated requests made as the user share that bucket. To isolate CI traffic, optionally configure a GitHub App installation token:

1. Create a GitHub App with repository `Contents: read and write` permission and install it only on `LawrenceBanes/sm64ios-private-builds`.
2. Add repository variable `SM64_PRIVATE_BUILD_USE_GITHUB_APP=true`.
3. Add Actions secrets `SM64_PRIVATE_BUILD_APP_ID` and `SM64_PRIVATE_BUILD_APP_PRIVATE_KEY`.

The workflow uses `actions/create-github-app-token` to mint an installation token scoped to that one repository with `Contents: write`. It is job-local and revoked by the action when the job ends. When the variable is not enabled, the existing PAT secret remains in use. No asset or IPA is uploaded to the public source repository or public Actions artifacts.

## Diagnostics and recovery

An exhausted primary quota is reported with the reset time and halts before asset downloads. A rate-limit response from a later release call also halts immediately without retrying; wait until the reported reset/Retry-After window before starting another run. An ordinary 403 points to vault installation, repository selection, or `Contents` permission instead. The workflow uploads only the asset-free safe diagnostics directory for seven days; the unsigned IPA and its checksum continue to go only to the private vault release.
