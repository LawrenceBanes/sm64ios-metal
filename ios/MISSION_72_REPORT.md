# Mission 72 — Compatibility Implementation Report

## Result

**PARTIAL — host-side compatibility increased; Apple build and device IPA blocked.**

Implementation branch: `mission-72-fast3d-compat`  
Starting commit: `93a8f67be79dee48f2b67013c7fdc8bcf70853db`  
Implementation commit: `27f201214973bf579b1619976a135f550876720e`

## Changes

- `tools/o2r_draw_trace.py` reads and validates bounded 64-byte O2R N64 fixed 16.16 matrices. It interprets hashed `MTX_OTR` and segmented `G_MTX` model-view operations (load, multiply, push, pop), rejects projection/non-affine use, snapshots the matrix at vertex-load time, and records it in deterministic draw traces.
- `tools/o2r_to_wsc3.py` bakes supported static affine transforms into WSC3 vertex positions. `LOADTILE` now lowers supported 4-, 8-, and 16-bit crops when source and TMEM strides agree; CI4 handles odd-nibble origins and packs the crop before palette expansion. Invalid alignment, bounds, or layout remains a conversion error.
- The dry-run and conversion reports include draw acceptance/rejection reasons, geometry/material/texture counts, collision and room/link coverage, used matrix resources, and bounded package/payload estimates.
- No WSC binary version, native Metal shader, gameplay, or runtime loader was changed. WSC1/WSC2/WSC3 package layouts remain untouched.

## Fixture evidence

The original authored two-room fixture combining opaque/translucent draws, CI4/CI8, two texture inputs, TLUT data, odd-nibble CI4 cropping, a hashed translation matrix, collision, safe spawns, and a room link converted deterministically:

- 4 of 4 candidate draws translated
- 2 rooms, 6 render triangles, 4 collision triangles
- 2 materials, 2 textures, 1 room link
- 1,006-byte WSC3 payload

This is host-side authored-fixture evidence, not an authentic Nintendo scene conversion or an iPhone rendering result.

## Validation

**PASS:** `tests/run_linux_checks.sh` completed with exit code 0 after the final implementation and tests. This includes the repository Linux regression suite and host syntax builds for existing iOS/Metal C interfaces. Existing warnings remain in `src/pc/platform.c` and `src/game/mario_actions_airborne.c`.

**PASS:** `tests/test_o2r_draw_trace.py` (11 tests), `tests/test_o2r_to_wsc3.py` (12 tests), and both focused suites under `PYTHONMALLOC=debug`.

**PASS:** `git diff --check`; worktree is clean after commit.

**NOT RUN:** ASan/UBSan Fast3D reference harnesses require pinned Shipwright/libultraship and OoT source checkouts, which are not present in the local workspace. The modified importer is Python; the Linux suite compiled the existing C runtime, but that is not sanitizer coverage of this parser.

**BLOCKED:** `xcodebuild` is not installed. GitHub API access currently fails with DNS resolution for `api.github.com`; `gh workflow list` cannot retrieve workflows. No Actions run, private upload, IPA, or physical-device test was performed.

## Boundaries and next step

Fog, vertex lighting, generated texture coordinates, non-equivalent alpha/coverage/depth modes, dynamic runtime display lists, actors, water boxes, camera scripts, and game events remain unsupported or explicitly diagnosed. No WSC4 change was justified by the authored fixture. The affine matrix path assumes a matrix is a static model/world transform; the importer cannot infer whether an O2R matrix encodes a runtime camera transform, so those inputs must not be baked as world geometry.

After GitHub connectivity is restored, push this isolated branch and run the existing manual asset-free Apple validation first. A full-game IPA additionally requires the authorized private base-asset bundle and private release access. No proprietary assets or generated worlds are present in this commit.
