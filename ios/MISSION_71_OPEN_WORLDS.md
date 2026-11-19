# Mission 71 — External worlds on iPhone

## Convert and audit

Use a local archive containing a scene you are authorized to adapt. The scene argument is its exact resource path. The converter runs on the host with Python 3.

```bash
python3 tools/o2r_to_wsc3.py /path/to/authorized.o2r myproject/scenes/atrium \
  --dry-run > /tmp/atrium-audit.json
python3 tools/o2r_to_wsc3.py /path/to/authorized.o2r myproject/scenes/atrium /tmp/atrium-install
```

Dry-run checks the same trace, texture, collision, spawn and package constraints as export. It writes JSON to standard output and creates no package or textures on disk. Exit 0 means compatible, and exit 2 means rejected. A report distinguishes archive entry count, reached resource headers, converted sources, material batches, render and collision triangles per room, and validated spawn records. Trace and draw failures carry the room, display list, packet offset and command when available. Room and collision failures appear under `scene_issues`. Counts describe work completed before a failure. Review all reported issues before attempting export.

Mission 71 accepts per-room static segment bindings through the optional layout file. For example, `"room_segments":{"myproject/room":["08@10=myproject/state-list"]}` binds segment 08 to byte 0x10 in that resource, relative to the display-list command stream for ODLT. The same `HEX[@OFFSET]=RESOURCE` syntax works with `--segment` for scene-wide defaults. Explicit room bindings take precedence. The tracer still checks resource types, vertex ranges, packet boundaries and recursion.

The importer can now bake a static, integer-aligned 8-bit or 16-bit `LOADTILE` rectangle into the WSC3 binding when source stride, TMEM line and render extent agree. Unsupported or ambiguous cases are rejected. The existing whole-image `LOADBLOCK` path remains unchanged. WSC1/WSC2/WSC3 layouts and native Metal shaders required no changes. Runtime-generated graphics commands, fog, calculated vertex lighting, unsupported alpha/depth modes, nibble-aligned or 32-bit subtexture loads, matrix transforms, water and game-specific warps still require separate work. See [Mission 70's format and layout notes](MISSION_70_O2R_WSC3.md).

## Build

The full USA target requires extracted stock SM64 inputs during compilation. Make stops first on `sound/sequences/us/` when extraction is skipped on a clean worktree. The authorized base bundle contains 34 sequence inputs and 219 sample inputs. Make uses these along with tracked sound bank definitions to generate `sound/sequences.bin`, `sound/sound_data.ctl`, `sound/sound_data.tbl` and the associated compiled C includes. Extracted textures and other mapped inputs also feed the executable and `res/base.zip`.

The established GitHub workflow can use the approved, checksum-backed private bundle `sm64-assets-m57c-20261117`. Dispatch `ios-metal-ipa.yml` on the Mission 71 branch with `full_game=true`, `scene_diagnostic=true` and `hat_movement=true`. Leave the optional Hyrule archive tag unset and connected world false. The full build enables `IOS_O2R_SCENE_RUNTIME=1 IOS_SCENE_DIAGNOSTIC=1 EXTERNAL_DATA=1`. The unsigned app and checksum go to the existing private vault. Public CI holds only safe diagnostics. The package validator checks both bundled original diagnostics, stock textures and audio, the compiled Documents loading controls, and the app identity. It rejects bundling an external world package or source archive for this configuration.

For a local macOS build, use the pinned SDL2 and Xcode setup in [the iOS build guide](BUILDING.md). From an isolated copy of this branch, run `python3 extract_assets.py us` with your authorized USA ROM at `baserom.us.z64`, or use `python3 ios/asset_bundle.py extract /path/to/verified/sm64-assets.tar.gz --root .`. Run the iPhoneOS Xcode build with the flags above, `NOEXTRACT=1`, `SM64_SKIP_CODESIGN=1`, `CODE_SIGNING_ALLOWED=NO`, and the local SDL2 paths. Run `ios/package_unsigned_ipa.py` on the resulting app, then `ios/validate_ipa.py --require-local-scene-loader --require-shitbox-identity --require-game-textures --require-system-deps` on the IPA.

## Device procedure

1. Use SideStore to sign the private unsigned IPA. Keep the existing app container if you need its saves. Launch once so iOS makes the app's Documents folder available.
2. Unzip the local conversion output on your computer or in Files. Copy the PNGs into `Documents/gfx/scenes/` first, then copy `4100.wsc` into `Documents/scenes/`. Merge subfolders when you have other local resources. Keep `conversion-report.json` and the source archive on the host.
3. Resume active Castle Grounds gameplay and choose **Gameplay → Load Local WSC3 Scene**. The built-in room slot holds one local scene at a time. Walk across the floor between rooms, reverse direction, and watch for the room transition in the diagnostics log.
4. Choose **Leave Local WSC3 Scene**, verify Castle Grounds movement, and repeat entry without restarting. Verify the WSC2 and WSC3 diagnostic entries separately.
5. With Mario back in Castle Grounds, temporarily move a referenced PNG out of the container and retry. Expect a rejection and a return to Castle Grounds. Restore the PNG and retry. The log should identify the missing texture during the failed attempt.

The bundled game uses SHITBOX LABZ INC., bundle ID `com.larry.shitboxdev`, and developer release `beta.069420INITIAL`. Host replay and Apple compilation do not establish device-level gameplay.
