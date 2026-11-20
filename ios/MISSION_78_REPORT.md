# Mission 78 — Hyrule scale and OoT region expansion

## Status

- **PASS — Host export:** Hyrule Field was regenerated at an effective 9/4 (2.25x) source scale, replacing the prior 7/4 export. A translation offset keeps baked coordinates inside the existing expanded collision grid and signed 16-bit WSC fields.
- **PASS — Host integration:** The same opt-in SM64 external-scene area can select Hyrule or Market, validate their spawn floors, unload, switch, return to the previous imported scene, reload, and tear down without leaked world-region registrations in the host integration harness.
- **PASS — Host rendering/collision probe:** Both real package exports passed the production WSC reader, floor query, display-list submission, and teardown probe. This is not a Metal pixel test.
- **PASS — Private incremental library overlay:** The checked overlay has only the two scene packages and updated library index/manifest; it contains no ROM, raw O2R source tree, or PNG payloads.
- **PASS — Local regressions:** `CC=clang tests/run_linux_checks.sh` passed. Relevant exporter, pass-state and IPA-validator tests passed (47 tests). The real Hyrule and Market probes passed. ASan/UBSan multi-scene tests passed with `ASAN_OPTIONS=detect_leaks=0`.
- **UNVERIFIED — Apple / device:** No Mission 78 Xcode/Metal build or iPhone gameplay has been performed yet. A private unsigned IPA is still required before the physical-device acceptance criterion can be assessed.

## Hyrule Field at 2.25x

Source: `scenes/shared/spot00_scene/spot00_scene`, OoT NTSC 1.0 archive marker `01 EC 70 11 B7`.

The old Mission 76 layout uses `--scale 7/4`. This export uses `--scale 9/4` once, with `--offset 4856 0 -16622`; runtime scaling remains disabled. The offset is a coordinate recentering needed because a zero-offset 2.25x export exceeds signed 16-bit coordinates. It does not change the scale ratio. Mario's size, collision capsule, movement values, UVs, colors, angles and material parameters remain native/unscaled.

- 844 candidate draws; all 844 translated, no trace issues
- 202 batches, 57 unique materials, 50 textures
- 1,639 render triangles; 1,579 collision triangles
- 16 accepted entrances; default spawn `(5216, 16, -13438)`
- WSC5 package: 121,562 bytes; package SHA-256 `a9cae3a2943981c85dab097ecf7f4437c42bf846f5b1f926d98fca23478f8203`
- Room bounds: `(-20002, -2464, -20571)..(20003, 1665, 20568)`; all bounds remain inside the expanded grid and signed 16-bit fields.

The 50 generated texture PNGs are byte-identical to files already in the supplied v1 library, so they are not duplicated in the overlay. Existing RT work was left intact; no RT renderer or world-capture path was changed. The imported scene uses the established SM64 Metal draw submission path.

## First additional region: OoT Market day

Source: `scenes/shared/market_day_scene/market_day_scene`, same NTSC 1.0 archive. It was selected because the complete scene, one room, collision header and 12 entrance rows are present and it converts without unresolved draw commands under an explicit static day-lighting/pass profile.

- 257 candidate draws; all 257 translated, no trace issues
- 8 opaque batches, 5 unique materials, 0 texture dependencies (source output is vertex/primitive colored)
- 510 render triangles; 384 collision triangles from 431 vertices
- 12 safe entrance rows; default spawn `(-9, 16, 1616)`
- WSC3 package: 28,542 bytes; package SHA-256 `785378cafceac3c3350c6b8a6a5bd0b3e18e39af62086eac2df9a0eb43a9cabe`

The profile explicitly chooses NTSC 1.0 lighting row 0 and environment RGBA `(128,128,128,128)`. Lighting is reconstructed as a static snapshot; time-of-day selection and dynamic scene state are not inferred. All 19 OoT collision surface classes are explicitly mapped to `SURFACE_DEFAULT`; this preserves floor geometry but not Zelda-specific surface effects. The source's camera, skybox, exits, sound, paths, actors, objects and scene gameplay metadata are not represented by WSC. The one-room scene does not exercise cross-room streaming.

## Multi-region loading and controls

Scene package IDs are `4100` for Hyrule and `4101` for Market. The existing reserved Castle Grounds external area remains the sole SM64 area; it selects a package by ID, opens it with the normal area load, and closes it on area unload. The Gameplay menu now offers Hyrule and Market entries, return to Castle Grounds, and return to the previous imported scene. This is a normal SM64 area transition, so standard warp/camera initialization runs; it is not a seamless or source-authentic OoT exit. Riding Yoshi or driving the kart remains disallowed during these developer scene changes. Source entrance tables are retained in packages, but actual OoT entrance-to-scene transitions are not mapped yet.

The generic loader accepts package IDs 4100–4199 and requires WSC3+ render materials for those imported packages. It reads from the existing Documents `oot-assets` library. Legacy Documents files take precedence; remove/backup a stale `Documents/scenes/4100.wsc` before applying the new overlay or the old scale can continue to load.

## Reproducible private overlay

The v1 library itself is unchanged. The generated incremental overlay is:

- `/tmp/OoT-Complete-Asset-Library-v1-overlay-mission78.zip`
- 3,671,662 compressed bytes; ZIP integrity passes
- SHA-256 `6369214dcb0c160e3f1ee66d256587187b3862ab19b5bee9000085707e024193`
- Entries: `oot-assets/manifest.json`, `oot-assets/runtime-index.tsv`, `oot-assets/scenes/4100.wsc`, and `oot-assets/scenes/4101.wsc`

Regeneration commands:

```sh
python3 tools/o2r_to_wsc3.py "$OOT_O2R" scenes/shared/spot00_scene/spot00_scene /tmp/hyrule-export \
  --layout ios/mission76-hyrule-layout.json --scale 9/4 --offset 4856 0 -16622 \
  --scene-id 4100 --expanded-grid --installed-library /path/to/oot-assets
python3 tools/o2r_to_wsc3.py "$OOT_O2R" scenes/shared/market_day_scene/market_day_scene /tmp/market-export \
  --layout ios/mission78-market-layout.json --scale 9/4 --scene-id 4101 --expanded-grid
python3 -m tools.oot_asset_library "$OOT_O2R" /tmp/oot-overlay.zip --update-from "$OOT_MASTER_ZIP" \
  --scene-export /tmp/hyrule-export --scene-export /tmp/market-export
```

## Region coverage

| Region | Source availability | Mission 78 conversion status |
|---|---|---|
| Hyrule Field | Present; 1 default room | 2.25x WSC5 export and host load/collision/draw probe pass |
| Market day | Present; 1 room | 2.25x WSC3 export and host load/collision/draw probe pass |
| Kokiri Forest (`spot04`) | Present; 3 rooms, 12 spawn/entrance rows | Not converted in this mission; historical trace requirements and unsupported runtime-generated display-list calls still need resolution |
| Other prioritized overworld regions | Source entries are retained in the master library | Not converted or individually audited here; no playability claim |

The runtime currently has developer menu entries for the two validated packages. Adding further scenes still requires a host export, a package ID/menu registration, and a private incremental library update. This is not an on-device O2R converter.

## Validation evidence

- Exporter unit tests cover serialized scene IDs, 9/4 affine transformation, unchanged non-spatial vertex attributes, source pass-state seeding, and profile rejection cases.
- Real archive conversion was repeated twice per scene; package bytes, texture output maps and conversion reports were deterministic.
- Hyrule probe: 1,579 collision triangles, spawn/floor query success, draw submission, 2 texture uploads, teardown.
- Market probe: 384 collision triangles, spawn/floor query success, 70 submitted probe triangles across 5 draw calls, 0 texture uploads as expected, teardown.
- Combined-scene host test: validates IDs 4100/4101, area activation, Market spawn, Hyrule↔Market selection, previous-scene return, repeated area reload and zero regions after teardown.
- Full `tests/run_linux_checks.sh`: passed. The only output warnings were existing host warnings in platform and hat-action source.
- ASan/UBSan scene lifecycle test: passed with leak detection disabled. LeakSanitizer itself failed in this host due to its ptrace restriction; no system setting was changed.

## Remaining limitations and next steps

1. Run the established Xcode 27/iPhoneOS workflow on this branch, validate the unsigned IPA, and privately publish it with the incremental overlay.
2. Have Larry test Hyrule scale, camera distance, floor behavior, Market rendering, Market collision, return-to-previous and return-to-Castle on iPhone. Host probe triangle counts do not prove visible Metal pixels.
3. If Market is visually valid, use its no-texture output as an easy second-scene runtime case. Then resume Kokiri, where dynamic display-list state and multi-room behavior are harder.
4. Audit scene entrance tables and source transition identifiers before adding automatic cross-scene triggers; do not infer topology from entrance rows alone.
5. Preserve the Hyrule collision mapping limitations and WSC draw/culling approximations in all later reports.
