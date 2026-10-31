#!/usr/bin/env python3
"""Static regression checks for the UIKit-to-SM64 touch path and frame probes."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GESTURE = (ROOT / "src/ios/ios/TouchControlsViewController.m").read_text()
CONTROLLER = (ROOT / "src/pc/controller/controller_touchscreen.m").read_text()
SDL = (ROOT / "src/pc/gfx/gfx_sdl2.m").read_text()
UIKIT = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
METAL = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()
MAIN = (ROOT / "src/pc/pc_main.m").read_text()
DISPLAY_LINK = (ROOT / "src/ios/FrameController.m").read_text()


def require(needle: str, source: str, description: str) -> None:
    if needle not in source:
        raise SystemExit(f"touch/frame diagnostic regression missing {description}: {needle}")


def main() -> None:
    require("locationInView:self.view", GESTURE, "UIKit touch coordinates are point-valued in the touch root view")
    require("touchEvent.x = position.x", GESTURE, "UITouch coordinates reach the controller callback")
    require("touchEvent.y = position.y", GESTURE, "UITouch y coordinates reach the controller callback")
    require("touch_down_callback((void *)&touchEvent)", GESTURE, "UIKit gesture dispatches touch down")
    require("receiver=%@ hitView=%@", GESTURE, "UIKit receiver and z-order hit-test diagnostics")
    require("callback=%d tracked=%lu", GESTURE, "touch callback installation and tracking diagnostics")
    require("topVisibleWindow=%@", GESTURE, "touch hit-test identifies a higher-level visible scene window")
    require("trackedTouchIDs", GESTURE, "stable touch identities across independent fingers")
    require("touch moved without tracked begin", GESTURE, "safe handling for orphaned touch motion")
    require("touch ended without tracked begin", GESTURE, "safe handling for orphaned touch end")
    require("gestureRecognizer.cancelsTouchesInView = NO", GESTURE, "gesture recognizer preserves UIKit view touch delivery")
    require("gestureRecognizer.delaysTouchesBegan = NO", GESTURE, "touchdown is not delayed behind recognizer resolution")
    require("UIKit touch view-controller responder began", GESTURE, "UIKit responder-chain delivery diagnostics")
    require("UIKit touch layout bounds=", GESTURE, "post-autoresize touch overlay geometry diagnostics")
    require("UIKit touch control layout tag=", GESTURE, "individual touch control frame diagnostics")
    require("convertPoint:CGPointZero toView:tcvc.view", CONTROLLER, "control bounds converted from image-local coordinates")
    if "convertPoint:frame.origin toView:tcvc.view" in CONTROLLER:
        raise SystemExit("touch hit-testing still converts a parent-space frame origin as an image-local point")
    require("Virtual control hit-test miss", CONTROLLER, "miss diagnostics with candidate geometry")
    require("Virtual control hit seq=", CONTROLLER, "matched virtual control diagnostics")
    require("Touch controller read heartbeat", CONTROLLER, "controller polling heartbeat")
    require("Virtual MENU input dispatched", CONTROLLER, "MENU dispatch diagnostics")
    require("UIKit gesture target action seq=", GESTURE, "UIGestureRecognizer action-state diagnostics")
    require("SDL event pump heartbeat", SDL, "SDL event polling heartbeat")
    require("SDL touch event type=down", SDL, "SDL finger event diagnostics")
    require("Touch input callbacks installed source=UIKit GestureRecognizer", MAIN, "actual UIKit callback route is explicit")
    if "wm_api->set_touchscreen_callbacks(" in MAIN:
        raise SystemExit("virtual controls unexpectedly switched to the SDL touch callback route")
    require("Game tick heartbeat", MAIN, "simulation/main-loop heartbeat")
    require("UIKit CADisplayLink heartbeat", DISPLAY_LINK, "UIKit display-link heartbeat")
    require("Metal presentation heartbeat", METAL, "Metal command submission heartbeat")
    require("Metal command buffer scheduled count=", METAL, "GPU command scheduling diagnostics")
    require("Metal CoreAnimation drawable presented count=", METAL, "periodic Core Animation display completion diagnostics")
    require("addPresentedHandler", METAL, "display callback registration before drawable presentation")
    if METAL.index("addPresentedHandler") > METAL.index("presentDrawable:submitted_drawable"):
        raise SystemExit("Metal display handler must be registered before scheduling drawable presentation")
    require("presentsWithTransaction = NO", METAL, "transaction scheduling disabled for command-buffer presentation")
    require("userInteractionEnabled", METAL, "Metal view interaction state diagnostic")
    require("UIKit SDL controller subview zIndex=", UIKIT, "post-attachment UIKit subview ordering diagnostics")
    require("sm64_diag_attach_to_game_controller(viewControllerPointer)", UIKIT, "diagnostic controls move into the SDL scene hierarchy")
    require("diagnosticWindow.rootViewController = nil", UIKIT, "extra diagnostics UIWindow is removed after SDL initialization")
    require("BOOL inside = CGRectContainsPoint(self.hitRect, point)", UIKIT, "diagnostic overlay passes through touches outside its panel")
    require("return inside;", UIKIT, "diagnostic overlay returns false outside its panel hit rectangle")
    require("diagnosticWindow.hidden = YES", UIKIT, "temporary higher-level startup window is hidden before gameplay")
    require("separate diagnostic UIWindow root released before containment", UIKIT, "temporary diagnostic window removed from gameplay scene")
    require("UIKit diagnostics overlay hit-test seq=", UIKIT, "diagnostics window/subview touch routing evidence")
    print("UIKit touch routing, transformed hit-test coordinates, controller polling, and simulation/presentation heartbeat checks passed")


if __name__ == "__main__":
    main()
