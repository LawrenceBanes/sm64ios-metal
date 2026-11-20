# Mission 78 progress

- Branch: `mission-78-oot-world-expansion`
- Base: Mission 77 `5376b9bc459e4a152e4a51fe3a0327774a728d35`
- Current implementation: 2.25x Hyrule export, authentic OoT Market day WSC3 export, package-ID selectable imported-region area, previous-scene switching, host tests, incremental library overlay.
- Local archive: `/home/larry/Downloads/oot.o2r` (NTSC 1.0; SHA-256 `e3bf3ae3565768998026e60113ed3fc506fc5415e23299de41b9b42a5fbc3807`).
- Master library: `/home/larry/Downloads/OoT-Complete-Asset-Library-v1.zip` (SHA-256 `2cbb0a595de9c7b2f214bbc61bdd8f3e7fae40a489f9537c172fe429368e8aeb`).
- Hyrule export: `/tmp/mission78-hyrule-225-final` (WSC5 ID 4100; 844/844 translated, 202 batches, 50 shared textures, 1,639 render / 1,579 collision triangles, 16 entrances; effective `9/4`, offset `[4856,0,-16622]`).
- Market export: `/tmp/mission78-market-225` (WSC3 ID 4101; 257/257 translated, 8 batches, no texture dependencies, 510 render / 384 collision triangles, 12 entrances; explicit static lighting row 0 and default-surface approximation).
- Combined host test assets: `/tmp/mission78-scenes-multi`.
- Private incremental overlay: `/tmp/OoT-Complete-Asset-Library-v1-overlay-mission78.zip`, SHA-256 `6369214dcb0c160e3f1ee66d256587187b3862ab19b5bee9000085707e024193` (4 entries, no full source tree or PNG duplicates).
- Commands passed: `CC=clang tests/run_linux_checks.sh`; `PYTHONPATH=tests:ios python3 -m unittest test_o2r_to_wsc3 test_fast3d_pass_lowering test_ipa_validation -v`; two actual scene probes; combined scene switch/return/reload/teardown probe; ASan/UBSan scene-package run with `ASAN_OPTIONS=detect_leaks=0`; Python syntax compilation.
- Pending: review diff; commit; push branch; run one existing full iPhoneOS IPA workflow; upload IPA and overlay only to private vault; verify remote artifact checksums; physical iPhone testing still required.
