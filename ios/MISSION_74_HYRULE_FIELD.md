# Mission 74 — Hyrule Field iPhone rendering test

## Test build

This build keeps the standard SM64 runtime authoritative and enables the existing external WSC scene slot in Castle Grounds. The developer Gameplay menu exposes **Load Hyrule Field (WSC5)** and **Leave Hyrule Field**. The external package and textures stay outside the app bundle and outside source control.

The supplied local OoT NTSC 1.0 archive is `oot.o2r` (revision marker `EC7011B7`). The verified export is Hyrule Field `spot00`, package ID 4100, SHA-256 `0b9c686560353c2f235dc8665d85c4439ae30691f7e9c00d943aa76322a56091`. It contains one room, 202 ordered render batches, 57 materials, 1,639 render triangles, 50 PNG textures, 1,579 collision triangles, and 14 retained safe entrances. Default entrance 0 is `(160,16,1415)`, yaw `-3641`; the production loader validates floor, nearby samples, ceiling, and wall clearance before accepting it.

## Copy the local scene to the iPhone

The IPA must be installed and opened once so iOS creates the app's Documents directory. The app exposes Documents in Files (`UIFileSharingEnabled` and `LSSupportsOpeningDocumentsInPlace`). The exporter output has this shape:

```text
scenes/4100.wsc
gfx/scenes/<50 referenced .rgba16.png files>
```

The verified local transfer archive is `/home/larry/Downloads/HyruleField-WSC5-Documents.zip` (SHA-256 `7c1f4737dff9c68f83d7ec0dfee6d1087b76594607a4bf407fdee92afa2a526b`). It contains exactly the WSC5 file and 50 referenced PNGs, with no O2R archive. To transfer it from Linux without a public upload, you can run a temporary same-network HTTP server from the Downloads directory (`python3 -m http.server 8765 --bind 0.0.0.0`), then open `http://<host-LAN-IP>:8765/HyruleField-WSC5-Documents.zip` in iPhone Safari and save it to Files. Stop the server after transfer. Extract the ZIP in Files; move `scenes/4100.wsc` and `gfx/scenes/` into **On My iPhone → SHITBOX LABZ INC.**, preserving the exact relative paths. Merge the `gfx/scenes` directory if it already exists; do not rename the WSC or texture files. Do not copy `oot.o2r` to the phone.

If the generated tree is not available, regenerate from the user-supplied archive and the same validated scene layout. The archive and output are private user-provided data; keep the generated package local and do not add it to Git or public CI artifacts.

## Device test

1. Launch SM64 and enter active Castle Grounds gameplay.
2. Open Gameplay → **Load Hyrule Field (WSC5)**. The game returns to Castle Grounds with a status message if the package is absent or rejected.
3. On success Mario should spawn at entrance 0 on the imported terrain. Use the normal stick, jump, and camera controls. Inspect the ground, nearby structures, material textures, and transitions in the rendered image.
4. Open Gameplay → **Leave Hyrule Field** to return to Castle Grounds, then load again to check teardown/re-entry.
5. Save `Documents/SM64Diagnostics.log` from Files after a short walk. It should include package format/counts, spawn validation/activation, texture diagnostics on demand, Metal encoded draw/triangle counts, and any failed asset references.

The `Metal frame encoded` counters report submitted draw batches/triangles before command-buffer completion. Texture decode/upload diagnostics confirm resource handling. Neither proves displayed GPU pixels. Physical-device screenshot/video remains the acceptance evidence.

The optional real-package host check can be repeated with `SM64_WSC5_PROBE_DIR=/tmp/mission73-hyrule-export-final-20261119 CC=clang python3 tests/test_world_scene_package.py`. It validates the production parser, room/collision registration, the entrance's inclusion in room bounds, a floor query, recorded draw submissions, texture decoding, and cleanup. Its probe projection is synthetic and is not the iPhone camera or Metal rasterizer.

## Known rendering limits

This is a static Hyrule Field subset, not a complete OoT runtime. The export uses the explicit daytime lighting snapshot and retains documented material approximations; it does not evaluate camera-dependent culling, and it omits water simulation and several scene metadata types. Some translucent ordering and unsupported source behavior may differ from OoT. Gameplay uses SM64 collision mapped to its default surface type. The build is only a rendering candidate until tested on an iPhone.
