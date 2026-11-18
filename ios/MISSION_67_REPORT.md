# Mission 67 report — scene-import foundation

## Outcome

**PARTIAL.** A generic, opt-in scene-package lifecycle and a synthetic O2R
resource-catalog test path are implemented. The full Ocarina of Time archive
was not processed, no Zelda-derived assets were generated, and no imported
scene is playable. The request to make all supported areas accessible is not
complete.

## Completed and verified

- Added `tools/o2r_inventory.py`, a bounded read-only O2R ZIP catalog pass for
  scene, vertex-array, collision-header, display-list, and texture records. It
  identifies scene/room references, alternates, entrance metadata, dependency
  coverage, and unsupported records. Version-1 catalog JSON is deterministic.
- Added `src/engine/world_scene.[ch]`, compiled only when
  `IOS_O2R_SCENE_RUNTIME=1`. It validates package/room/entrance/exit metadata,
  keeps source data active when destination loading fails, activates a target
  room before releasing the source scene, and manages nearby room residency
  using a caller-provided maximum per-tick travel bound.
- Added `world_region_unregister()` so a provider can free a room's owned
  collision surfaces and partition nodes during unload.
- Added sampled display-list section diagnostics in the existing renderer;
  they count interpreter-side rejects and accepted triangles without changing
  draw state. They are not GPU-visibility proof.
- Corrected an iOS Metal combiner representation gap: the packed shader mux
  cannot encode RDP alpha mux `ONE`. The display-list interpreter now tags the
  exact constant-one expression (`D=ONE` and either `A=B` or `C=0`), and the
  MSL generator emits literal alpha `1.0`. Host tests verify interpreter-to-
  shader propagation and generated source. Apple Metal compilation and the
  effect on the actual imported field remain unverified.
- Added baseline and architecture notes in `MISSION_67_BASELINE.md` and
  `O2R_SCENE_RUNTIME.md`.

## Validation

**PASS:** `CC=clang sh tests/run_linux_checks.sh` completed successfully after
the final scene-runtime, combiner, and test changes. It covered the existing gameplay,
Metal-source, Hyrule, connected-world, collision, and packaging regressions.
`tests/test_o2r_inventory.py` uses synthetic archives only. The dedicated
world-scene test covers metadata rejection, room prefetch and pinning, rollback
after failed activation, failed/successful exits, return traversal, and close.

**PASS:** `CC=clang sh tests/run_world_scene_test.sh` and
`CC=clang sh tests/run_world_region_test.sh` passed. The scene-runtime test also
passed AddressSanitizer and UndefinedBehaviorSanitizer with leak detection
disabled; this host's LeakSanitizer cannot run under its ptrace restrictions.

**PASS:** `git diff --check` passed. The Makefile feature default is `0`, and
the CI build helper passes the explicit `IOS_O2R_SCENE_RUNTIME` setting through
Xcode to the actual Make compile flags. Linux tests compile the object with
the feature off and on and verify the symbols/flags.

**UNVERIFIED:** Apple SDK compilation, Apple's Metal compiler, an IPA, and
physical iPhone rendering. `xcodebuild`, `xcrun`, and `metal` are not installed
on this CachyOS host. No GitHub Actions or Codemagic build was dispatched.

## Blocking work for an actual playable imported world

1. A supported, format-neutral importer must convert every requested scene's
   display lists, textures/palettes, collision, rooms, lighting, and entrance
   graph into a bounded runtime package. The current O2R catalog is metadata
   only; the existing lowering tool still targets one selected scene and
   emits level-specific C.
2. Implement an iOS package provider that decodes that package, binds materials
   and textures to the existing Metal/Gfx path, registers collision through
   `world_region`, and owns resource references until GPU use completes. No
   such provider or package ABI is implemented yet.
3. Wire a real scene entry/exit trigger and package registry into gameplay.
   No tree-triggered destination exists in this branch.
4. Validate geometry scale, missing references, collision, camera, transitions,
   memory bounds, and visuals on an iPhone before describing any scene as
   playable.

No private IPA was created or published. The existing connected-world and
Hyrule implementations remain intact and their assets were not modified.
