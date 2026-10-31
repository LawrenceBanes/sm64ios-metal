#!/usr/bin/env python3
"""Source-level regression checks for UIKit diagnostic-controller migration."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UIKIT = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise SystemExit(f"unterminated function: {signature}")


def require(needle: str, source: str, description: str) -> None:
    if needle not in source:
        raise SystemExit(f"diagnostics migration regression missing {description}: {needle}")


def main() -> None:
    attach = function_body(UIKIT, "static void sm64_diag_attach_to_game_controller")
    create = function_body(UIKIT, "static void sm64_diag_create_window")
    host_init = function_body(UIKIT, "void gfx_uikit_init(")

    # Startup uses a temporary scene window. Before containment, clear that
    # window's root ownership while a strong temporary reference keeps the VC alive.
    require("sm64DiagnosticsWindow.rootViewController = controller", create, "startup diagnostics root controller")
    require("[controller retain]", attach, "temporary controller ownership across root transfer")
    require("diagnosticWindow.hidden = YES", attach, "temporary diagnostics window hidden before reparenting")
    root_release = attach.index("diagnosticWindow.rootViewController = nil;")
    child_adoption = attach.index("[host addChildViewController:controller];")
    if root_release >= child_adoption:
        raise SystemExit("diagnostic UIWindow must release its root controller before SDL adopts it as a child")

    # Same-host repeats must not call addChild again. A move from a different
    # container must finish the old containment lifecycle first.
    repeated_guard = attach.index("if (controller.parentViewController == host)")
    repeated_return = attach.index("return;", repeated_guard)
    if repeated_return >= child_adoption:
        raise SystemExit("repeated attachment to the same SDL host must return before addChildViewController")
    require("[controller willMoveToParentViewController:nil]", attach, "old parent receives willMove(nil)")
    require("[controller removeFromParentViewController]", attach, "old parent containment is removed before reparenting")
    require("[host.view addSubview:controller.view]", attach, "diagnostics view is placed in the SDL view hierarchy")
    require("[controller didMoveToParentViewController:host]", attach, "new containment lifecycle is completed")
    view_insertion = attach.index("[host.view addSubview:controller.view]", child_adoption)
    did_move = attach.index("[controller didMoveToParentViewController:host]", view_insertion)
    if not (child_adoption < view_insertion < did_move):
        raise SystemExit("SDL child adoption, view insertion, and didMove calls are out of UIKit order")
    require("[controller release]", attach, "temporary controller retain is balanced after attachment")

    # Diagnostics stays above Metal but only its panel/probe owns hit testing.
    require("sm64_diag_attach_to_game_controller(viewControllerPointer)", host_init, "startup migration is wired into UIKit host setup")
    require("BOOL inside = CGRectContainsPoint(self.hitRect, point)", UIKIT, "overlay receives touches only in its active hit region")
    require("return inside;", UIKIT, "overlay passes other touches through to the game controls")
    print("UIKit diagnostics startup transfer, root release ordering, repeat attachment, and touch pass-through checks passed")


if __name__ == "__main__":
    main()
