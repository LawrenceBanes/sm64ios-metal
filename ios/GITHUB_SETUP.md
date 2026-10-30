# GitHub Actions setup

The checked-out `origin` is `ckosmic/sm64ex-ios`, where the connected GitHub app has read-only permission. The repository currently has no Actions workflows or workflow runs. The local validation workflow is ready in `.github/workflows/metal-apple-validation.yml`; it runs the full Linux validation suite on Ubuntu, then uses a macOS runner to compile/link the isolated Metal backend for iOS device and Simulator SDKs and compile generated shaders with Apple's Metal tools. Its logs and unsigned validation executables/metallibs are uploaded as a workflow artifact. It does not build the game or sign an IPA.

To run the workflow from a repository you control:

1. Create an empty repository under `LawrenceBanes` (for example, `sm64ios-metal`). Do not initialize it with a README or license, so the existing local history can be pushed as-is.
2. Authenticate GitHub CLI on the development machine with `gh auth login`; do not put a token in a remote URL or commit credentials.
3. Add the new repository as a separate remote, preserving the upstream:

   ```sh
   git remote add personal https://github.com/LawrenceBanes/sm64ios-metal.git
   git push -u personal ios
   ```

The push triggers the workflow on branch `ios`. The workflow can also be run manually from the repository's Actions page after the branch is pushed. Choose the repository's visibility and Actions policy there. No ROM, extracted game assets, signing material, or third-party texture pack is required by the validation workflow.
