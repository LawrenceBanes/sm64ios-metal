# Mission 67F — Scene runtime diagnostic

## Implementation checkpoint

- Branch `mission-67f-scene-diagnostic` starts at Mission 67E `f4222b205952b7e362753490ef8a5e3d3f28b136`. It already includes the unified gameplay base `a173f28b6130e58982c6d427f2bbbb7fabbc238e`.
- The opt-in `IOS_SCENE_DIAGNOSTIC=1` requires `IOS_O2R_SCENE_RUNTIME=1`, `EXTERNAL_DATA=1`, and `TARGET_IOS=1`. It adds a developer-only Castle Grounds area without adding a level or changing any existing area ID. The full iPhone build enables hat movement and retains existing gameplay. The original diagnostic has no O2R dependency.
- The game enters and leaves through the normal SM64 area warp and camera initialization. WSC2 supplies collision, opaque and translucent lists, and two PNGs built from authored CI4 and CI8 palettes. The room includes a walkable ramp and elevated platform. A lower safety floor allows a return if the package fails to load.
- The release build places four authored files in `res/base.zip` under `scene-diagnostic/` and verifies them before private upload. Ordinary WSC1/WSC2 scene packages still load from Documents.
- Linux replay mounts the actual generated pack, records 72 opaque and 2 translucent triangles, checks texture upload bytes and depth settings, queries the flat floor and ramp, and validates recovery and cleanup over two visits. It also exercises WSC1 documents and WSC2 room transitions using the prior fixtures. The mocked Xcode path compiles Castle Grounds with and without the diagnostic flags.

## Device checklist

1. Sign the unsigned IPA through SideStore or import it into the existing LiveContainer setup. Launch, load a save, and reach Peach's Castle Grounds. Be on foot in active gameplay.
2. Open native diagnostics and choose **Cheats / Play → Enter / leave WSC2 diagnostic**. Walk and jump on the blue-gray floor, then climb the green ramp on the right. The left upper checker should stay still and the narrow strip below it should scroll. On the right, the striped cyan glass should blend over the orange board while the dark post in front remains opaque.
3. In the same menu choose **Probe WSC2 failed-load recovery**. The scene and collision should stay active. The event log should contain `WSC2 failure probe PASS`. The probe deliberately attempts to load a four-byte truncated package from the bundled diagnostic namespace.
4. Leave through the same menu action and re-enter. Look for `WSC2 diagnostic entry` with result `0` and floor height `0.0`, and `WSC2 diagnostic exit activeRooms=0`. Export `SM64Diagnostics.log` using the native Share action or Files if behavior differs.

Actual iPhone rendering, control of the player through the warp, and install/signing remain device checks. The diagnostic contains no converted Kokiri Forest content.
