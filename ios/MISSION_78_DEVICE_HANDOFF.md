# Mission 78 — iPhone test handoff

## Build and asset requirements

The build uses the existing `[build-ipa] [scene-diagnostic]` full-game workflow and `IOS_SCENE_DIAGNOSTIC=1` / `IOS_O2R_SCENE_RUNTIME=1`. The IPA must not contain O2R source files or scene packages. The base SM64 extracted assets come from the established private build path. The OoT package overlay is installed separately into the existing `Documents/oot-assets/` directory.

Before applying the overlay, back up or remove a stale legacy `Documents/scenes/4100.wsc`; legacy files win over the library index. Extract the private Mission 78 overlay so its `oot-assets/manifest.json`, `runtime-index.tsv`, and `scenes/4100.wsc` / `4101.wsc` overwrite those same paths in the installed library. The master v1 package and its existing PNG resources are reused; no new texture files are needed for the two current scenes.

## Device checks

1. Install the unsigned IPA with the existing SideStore flow and launch the game.
2. In Castle Grounds, open **Gameplay → Load Hyrule Field (WSC5)**. Confirm Mario appears on visible terrain at the 2.25x export, inspect structures/fencing at multiple draw-distance settings, and walk/jump across the field.
3. Open **Gameplay → Load OoT Market**. Confirm the Market geometry appears, Mario stands on its collision, and movement remains stable.
4. Open Gameplay and choose **Return to previous imported scene**. Confirm Hyrule reloads with its floor present.
5. Choose **Return to Castle Grounds**, verify ordinary Castle Grounds gameplay, then reload both imported scenes.
6. Capture a short screen recording and the bounded iOS diagnostics log. Report spawn coordinates, any blank geometry/materials, floor misses, camera clipping, transition failures, and approximate frame rate.

These are normal SM64 area transitions from a developer menu; they are not native OoT door/exit transitions. The kart and Yoshi must be dismounted before selecting a scene. iPhone Metal pixels, camera quality, performance, and actual player navigation remain unverified until this test is performed.
