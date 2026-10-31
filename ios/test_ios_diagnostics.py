#!/usr/bin/env python3
"""Static integration checks for the on-device iOS diagnostic path."""

import plistlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UIKIT = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
SDL = (ROOT / "src/pc/gfx/gfx_sdl2.m").read_text()
METAL = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
HOST = (ROOT / "src/pc/pc_main.m").read_text()
PLIST = plistlib.loads((ROOT / "sm64ios/sm64ios/Info.plist").read_bytes())


def require(text: str, source: str, name: str) -> None:
    if text not in source:
        raise SystemExit(f"iOS diagnostics integration missing: {name}")


def main() -> None:
    require('SM64Diagnostics.log', UIKIT, "persistent Documents log")
    require('synchronizeFile', UIKIT, "frequent log flush")
    require('UIActivityViewController', UIKIT, "native share sheet")
    require('SM64DiagnosticsViewController', UIKIT, "native diagnostic panel")
    require('MTLClearColorMake(0.95, 0.0, 0.65, 1.0)', UIKIT, "independent Metal color probe")
    require('presentDrawable:drawable', UIKIT, "native Metal probe presentation")
    require('initWithWindowScene:scene', UIKIT, "scene-attached diagnostic window")
    require('SDL_GetCurrentVideoDriver()', SDL, "actual SDL video driver")
    require('SDL_GetWindowWMInfo', SDL, "SDL to UIKit window relationship")
    require('Metal renderer selected; attaching MTKView', METAL, "actual renderer selection")
    require('currentRenderPassDescriptor', METAL, "Metal game drawable acquisition")
    require('Metal presentation submitted', METAL, "game drawable presentation checkpoint")
    require('MTLCommandBufferStatusError', METAL, "GPU command completion errors")
    require('gfx_ios_diag_log("Selected graphics backend=RAPI_METAL', HOST, "selected rendering API")
    require('gfx_ios_diag_mark_first_game_frame', HOST + METAL, "first game frame marker")
    if PLIST.get("UIFileSharingEnabled") is not True:
        raise SystemExit("Info.plist must expose Documents through the Files app")
    if PLIST.get("LSSupportsOpeningDocumentsInPlace") is not True:
        raise SystemExit("Info.plist must allow Files to access app Documents")
    print("iOS persistent diagnostics, native UI, Metal probe, and pipeline instrumentation checks passed")


if __name__ == "__main__":
    main()
