# SM64 iOS Metal Development Policy

`main` is the canonical, continuously integrated development branch. Its history starts from the published unified-build baseline. Do not reset, force-push, or rewrite shared history.

## Starting work

- Fetch `origin` and start new feature work from the latest `origin/main`.
- Use a dedicated feature branch and isolated Git worktree for each mission, especially when another agent is active.
- Do not develop directly on `main`.
- Preserve active worktrees and uncommitted changes. Never switch, reset, clean, or rebase another agent's worktree without its owner's request.
- Mission 81 (`mission-81-shader64-rendergraph`) is active work. Keep it isolated until its owner marks it complete and its tests pass; then integrate it with a normal merge into the latest `main` and resolve conflicts without overwriting either side.

## Integration and release

- A feature is ready to merge only after its relevant Linux regressions, configuration checks, and integration validation pass. Run the Apple workflow for changes that affect the iOS app, renderer, shaders, packaging, or build configuration.
- Merge completed feature branches into the latest `main` with a normal merge commit. Preserve ancestry and all integrated behavior. Never force-push.
- Do not merge unfinished, conflicting, or failing work. Keep it on its feature branch and report the blocker.
- A push to `main` runs the Linux checks first, then the full `xcode-27` iPhoneOS arm64 build, IPA validation, and private-vault delivery. The IPA is produced only if the Linux job succeeds. This is the automatic integration build; do not dispatch a duplicate run for the same commit.
- Keep private base-game assets and generated proprietary worlds in the authorized private asset repository. Never put those assets, credentials, or asset-bearing IPA files in public source history or public CI artifacts.
- Preserve the app identity, normal SM64 gameplay, and all integrated feature systems: mods/Lua/DynOS, character and movement systems, connected-world/Hyrule support, WSC loaders and diagnostics, Metal enhancements, texture support, and existing build flags. Optional features may remain disabled in the standard IPA when their asset or device requirements are not supplied; retain their opt-in build paths and tests.
- When changing build flags or source registration, extend regression checks so CI catches accidental feature removal or disabling. Do not weaken tests to make a merge pass.

## Before completing a mission

1. Rebase or merge the latest `origin/main` into the feature branch without rewriting published commits.
2. Run the applicable host tests and configuration checks; run the Apple workflow for iOS-facing changes.
3. Review the merge result for preserved features and a clean worktree.
4. Merge to `main` only after all checks pass. Confirm the main-branch workflow produced the unified IPA and verify its private release asset/checksum.
5. Report the commit, tests, run, private IPA, and any features intentionally left disabled or blocked.
