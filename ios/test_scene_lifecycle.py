#!/usr/bin/env python3
"""Static checks for the iOS scene lifecycle and SDL UIKit window bridge."""

import plistlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INFO_PLIST = ROOT / "sm64ios/sm64ios/Info.plist"
SCENE_SOURCE = ROOT / "src/pc/gfx/gfx_uikit.m"
GFX_HEADER = ROOT / "src/pc/gfx/gfx_uikit.h"
SDL_PATCH = ROOT / "ios/patches/SDL2-2.32.10-uikit-scenes.patch"
SDL_BUILDER = ROOT / "ios/build_sdl2_ios.sh"


def main() -> None:
    info = plistlib.loads(INFO_PLIST.read_bytes())
    manifest = info.get("UIApplicationSceneManifest")
    if not isinstance(manifest, dict):
        raise SystemExit("Info.plist must define UIApplicationSceneManifest")
    if manifest.get("UIApplicationSupportsMultipleScenes") is not False:
        raise SystemExit("SM64 must declare a single application window scene")
    configurations = manifest.get("UISceneConfigurations", {}).get("UIWindowSceneSessionRoleApplication", [])
    if not any(
        config.get("UISceneConfigurationName") == "Default Configuration"
        and config.get("UISceneDelegateClassName") == "SM64SceneDelegate"
        for config in configurations
        if isinstance(config, dict)
    ):
        raise SystemExit("Info.plist must configure SM64SceneDelegate for the application window scene")

    source = SCENE_SOURCE.read_text()
    required_scene_methods = (
        "@interface SM64SceneDelegate : UIResponder <UIWindowSceneDelegate>",
        "scene:(UIScene *)scene willConnectToSession:",
        "sceneDidDisconnect:(UIScene *)scene",
        "gfx_uikit_window_scene_for_screen(UIScreen *screen)",
        "UIApplication sharedApplication].connectedScenes",
    )
    for text in required_scene_methods:
        if text not in source:
            raise SystemExit(f"scene lifecycle source is missing: {text}")

    header = GFX_HEADER.read_text()
    if "gfx_uikit_window_scene_for_screen(UIScreen *screen)" not in header:
        raise SystemExit("scene resolver must be declared to the SDL UIKit window bridge")
    for preserved in (
        "mainWindow = [[[UIApplication sharedApplication] delegate] window]",
        "instantiateViewControllerWithIdentifier:@\"TouchControlsViewController\"",
        "gfx_uikit_set_touchscreen_callbacks",
    ):
        if preserved not in source:
            raise SystemExit(f"UIKit host behavior unexpectedly missing: {preserved}")

    patch = SDL_PATCH.read_text()
    if "initWithWindowScene:windowScene" not in patch or "gfx_uikit_window_scene_for_screen" not in patch:
        raise SystemExit("SDL2 UIKit patch must create its primary window from the connected scene")
    if "} else\n+#endif\n+        {" not in patch:
        raise SystemExit("SDL2 UIKit patch must retain the existing external-screen path")
    builder = SDL_BUILDER.read_text()
    if "git -C \"$SDL2_SOURCE\" apply --check \"$SCENE_PATCH\"" not in builder:
        raise SystemExit("pinned SDL2 build must validate and apply the scene lifecycle patch")
    if "if (data.uiscreen != [UIScreen mainScreen]) {" not in builder:
        raise SystemExit("pinned SDL2 build must verify its existing external-screen handling")
    print("iOS scene manifest, delegate, and SDL window-scene bridge checks passed")


if __name__ == "__main__":
    main()
