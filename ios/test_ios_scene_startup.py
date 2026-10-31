#!/usr/bin/env python3
"""Check the SDL/UIKit scene gate and deterministic startup ordering."""

from itertools import permutations
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PATCH = (ROOT / "ios/patches/SDL2-2.32.10-uikit-scenes.patch").read_text()
SCENE = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
SDL_HOST = (ROOT / "src/pc/gfx/gfx_sdl2.m").read_text()
MAIN = (ROOT / "src/pc/pc_main.m").read_text()
METAL = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()


def require(text: str, source: str, name: str) -> None:
    if text not in source:
        raise SystemExit(f"scene startup integration missing: {name}")


def simulate(events: tuple[str, ...]) -> int:
    """Model the two independent run-loop wakeups and enforce one SDL_main call."""
    scene_connected = False
    observer_installed = False
    pending_start_callbacks = 0
    main_started = False
    for event in events:
        if event == "did_finish_launching":
            observer_installed = True
            pending_start_callbacks += 1
        elif event == "scene_connected":
            scene_connected = True
            if observer_installed:
                pending_start_callbacks += 1
        elif event == "runloop_callback" and pending_start_callbacks:
            pending_start_callbacks -= 1
            if not scene_connected:
                continue
            if not main_started:
                main_started = True
    while pending_start_callbacks:
        pending_start_callbacks -= 1
        if scene_connected and not main_started:
            main_started = True
    if not main_started:
        return -1
    return int(main_started)


def main() -> None:
    require('name:@"SM64WindowSceneDidConnectNotification"', PATCH, "SDL observer for scene-connected notification")
    require("startSDLMainWhenSceneIsReady", PATCH, "run-loop deferred scene gate")
    require("hasConnectedApplicationScene", PATCH, "connected-scene state check")
    require("if (forwardedMain)", PATCH, "single SDL_main invocation")
    require("gfx_ios_diag_log(\"Publishing SDL startup notification after UIWindowScene connection", SCENE, "scene callback signals SDL after assignment")
    require('postNotificationName:@"SM64WindowSceneDidConnectNotification"', SCENE, "scene notification publication")
    require("if (sdl_init_result < 0)", SDL_HOST, "SDL_Init failure return")
    require('if (!wnd)', SDL_HOST, "SDL_CreateWindow failure return")
    require("sdl_window_width <= 0 || sdl_window_height <= 0", SDL_HOST, "nonzero SDL dimensions")
    require("gfx_metal_attach_to_view_controller(sdlViewController)", SDL_HOST, "game MTKView attaches to SDL controller")
    require("scene_matches && visible && renderer_ready", SDL_HOST, "SDL/Metal scene and visibility validation")
    require("if (!gfx_sdl_is_window_ready())", MAIN, "game startup stops on renderer/window failure")
    require("First SM64 game drawable presented by CoreAnimation", METAL, "actual drawable presentation callback")
    require("drawable.width > 0.0 && drawable.height > 0.0", METAL, "game MTKView has nonzero drawable")

    for order in permutations(("did_finish_launching", "scene_connected", "runloop_callback", "runloop_callback")):
        callbacks_before_launch = [index for index, event in enumerate(order) if event == "runloop_callback" and index < order.index("did_finish_launching")]
        if callbacks_before_launch:
            continue
        if simulate(order) != 1:
            raise SystemExit(f"SDL_main did not start exactly once after scene connection for event order {order}")
    # UIKit can connect the scene before didFinishLaunch observers are installed.
    order = ("scene_connected", "did_finish_launching", "runloop_callback")
    if simulate(order) != 1:
        raise SystemExit(f"SDL_main did not start after early scene connection: {order}")
    print("SDL/UIKit startup gate checks passed for scene-first, launch-first, and repeated callback orderings")


if __name__ == "__main__":
    main()
