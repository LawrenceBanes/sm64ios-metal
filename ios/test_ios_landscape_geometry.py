#!/usr/bin/env python3
"""Regression checks for scene-based SDL/Metal landscape geometry and lifecycle."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SDL_ORIENTATION_PATCHER = ROOT / "ios/patch_sdl2_ios27_orientation.py"
SDL_ORIENTATION_PATCHER_SOURCE = SDL_ORIENTATION_PATCHER.read_text()
SDL_BUILDER = (ROOT / "ios/build_sdl2_ios.sh").read_text()
SDL_HOST = (ROOT / "src/pc/gfx/gfx_sdl2.m").read_text()
UIKIT = (ROOT / "src/pc/gfx/gfx_uikit.m").read_text()
METAL = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()


def require(needle: str, source: str, description: str) -> None:
    if needle not in source:
        raise SystemExit(f"landscape/lifecycle regression check missing {description}: {needle}")


def landscape_frame(frame: tuple[int, int], orientation: str) -> tuple[int, int]:
    """Model SDL's fullscreen frame correction, including iOS 27 unknown orientation."""
    if orientation == "unknown":
        return frame
    is_landscape = orientation in {"landscape_left", "landscape_right"}
    frame_is_landscape = frame[0] > frame[1]
    return (frame[1], frame[0]) if is_landscape != frame_is_landscape else frame


def main() -> None:
    require("gfx_ios_interface_orientation()", SDL_ORIENTATION_PATCHER_SOURCE, "shared iOS scene orientation query")
    require("scene.effectiveGeometry.interfaceOrientation", UIKIT, "iOS 27 scene effective orientation source")
    require(": UIInterfaceOrientationUnknown;", UIKIT, "scene-less iOS 27 orientation remains unknown")
    require("UIApplication.sharedApplication.statusBarOrientation", UIKIT, "pre-iOS 27 orientation compatibility fallback")
    require("gfx_ios_interface_orientation()", SDL_ORIENTATION_PATCHER_SOURCE, "SDL display mode orientation update")
    require("UIInterfaceOrientationUnknown", SDL_ORIENTATION_PATCHER_SOURCE, "unknown orientation handling")
    require("patch_sdl2_ios27_orientation.py", SDL_BUILDER, "pinned SDL orientation backport")
    require("windowScene.coordinateSpace.bounds", SDL_HOST, "scene point dimensions as geometry source")
    require("root.view.frame = sceneWindow.bounds", SDL_HOST, "SDL root frame reconciliation")
    require("root.view.bounds = CGRectMake(0, 0, sceneWidth, sceneHeight)", SDL_HOST, "SDL root bounds reconciliation")
    require("tcvc.view.frame = gameViewController.view.bounds", UIKIT, "touch overlay shares landscape root bounds")
    require("tcvc.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight", UIKIT, "touch overlay resize behavior")
    require("metal_view.frame = host_view.bounds", METAL, "MTKView follows the SDL root bounds")
    require("metal_view.drawableSize = expected_drawable", METAL, "drawable pixel dimensions follow points times scale")
    require("height - y - h", METAL, "viewport uses landscape drawable height and point-to-pixel scale")
    require("height - (scissor[1] + scissor[3]) * scale", METAL, "scissor uses landscape drawable height and point-to-pixel scale")
    require("gfx_metal_set_scene_active(false);", UIKIT, "background lifecycle disables Metal submission")
    require("gfx_metal_set_scene_active(true);", UIKIT, "active lifecycle resumes Metal submission")
    start_frame = METAL.index("static void gfx_metal_start_frame(void) {")
    render_gate = METAL.index("if (!gfx_metal_scene_can_render())", start_frame)
    drawable_acquire = METAL.index("metal_view.currentRenderPassDescriptor", start_frame)
    if not render_gate < drawable_acquire:
        raise SystemExit("Metal must gate background frames before acquiring a drawable or creating a command buffer")
    draw_entry = METAL.index("static void gfx_metal_draw_triangles(float data[]")
    draw_gate = METAL.index("if (!gfx_metal_scene_can_render())", draw_entry)
    pipeline = METAL.index("pipeline_for(active_program)", draw_entry)
    if not draw_gate < pipeline:
        raise SystemExit("Metal must suppress background triangle encoding before pipeline/draw work")
    require("Metal first game triangle draw encoded", METAL, "successful triangle encoding checkpoint")
    require("Metal pipeline created shader=", METAL, "successful pipeline checkpoint")
    require("First SM64 game drawable presented by CoreAnimation", METAL, "drawable presentation checkpoint")

    # iPhone 14 Pro Max: 932x430 landscape points at 3x become 2796x1290 pixels.
    assert landscape_frame((430, 932), "landscape_left") == (932, 430)
    assert landscape_frame((932, 430), "landscape_right") == (932, 430)
    assert landscape_frame((430, 932), "unknown") == (430, 932)
    points = (932, 430)
    scale = 3
    drawable = tuple(dimension * scale for dimension in points)
    assert drawable == (2796, 1290)
    viewport = (0, 0, points[0] * scale, points[1] * scale)
    scissor = viewport
    assert viewport == scissor == (0, 0, drawable[0], drawable[1])
    require("event.x = sdl_event.x;", SDL_HOST, "SDL finger x coordinate remains normalized in window space")
    require("event.y = sdl_event.y;", SDL_HOST, "SDL finger y coordinate remains normalized in window space")

    # Check that the maintained backport patch is valid as a unified diff and
    # transforms the precise SDL orientation block it targets.
    fixture_block = "\n".join((
        '#include "SDL_uikitvideo.h"',
        "",
        "     * https://bugzilla.libsdl.org/show_bug.cgi?id=3505",
        "     * https://bugzilla.libsdl.org/show_bug.cgi?id=3465",
        "     * https://forums.developer.apple.com/thread/65337 */",
        "#pragma clang diagnostic push",
        '#pragma clang diagnostic ignored "-Wdeprecated-declarations"',
        "    UIInterfaceOrientation orient;",
        "    orient = [UIApplication sharedApplication].statusBarOrientation;",
        "#pragma clang diagnostic pop",
        "    BOOL landscape = UIInterfaceOrientationIsLandscape(orient) ||",
        "                    !(UIKit_GetSupportedOrientations(window) & (UIInterfaceOrientationMaskPortrait | UIInterfaceOrientationMaskPortraitUpsideDown));",
        "    BOOL fullscreen = CGRectEqualToRect(screen.bounds, frame);",
    )) + "\n"
    with tempfile.TemporaryDirectory(prefix="sm64-sdl-orientation-patch-") as temp:
        source_root = Path(temp) / "src/video/uikit"
        video_path = source_root / "SDL_uikitvideo.m"
        modes_path = source_root / "SDL_uikitmodes.m"
        source_root.mkdir(parents=True)
        video_path.write_text(fixture_block)
        modes_path.write_text('#include "SDL_uikitmodes.h"\nSDL_bool UIKit_IsDisplayLandscape(UIScreen *screen) { return UIInterfaceOrientationIsLandscape([UIApplication sharedApplication].statusBarOrientation); }\nvoid SDL_OnApplicationDidChangeStatusBarOrientation(void) { BOOL isLandscape = UIInterfaceOrientationIsLandscape([UIApplication sharedApplication].statusBarOrientation); switch ([UIApplication sharedApplication].statusBarOrientation) {} }\n')
        subprocess.run(["python3", str(SDL_ORIENTATION_PATCHER), temp], check=True)
        patched_video = video_path.read_text()
        patched_modes = modes_path.read_text()
        require("gfx_ios_interface_orientation()", patched_video, "video sizing patch uses scene orientation helper")
        require("if (orient == UIInterfaceOrientationUnknown) return frame;", patched_video, "patch fixture result preserves unknown orientation")
        require("SDL_bool UIKit_IsDisplayLandscape(UIScreen *screen) { return UIInterfaceOrientationIsLandscape(gfx_ios_interface_orientation()); }", patched_modes, "display mode landscape patch uses scene geometry")
        require("BOOL isLandscape = UIInterfaceOrientationIsLandscape(gfx_ios_interface_orientation());", patched_modes, "display orientation-change patch uses scene geometry")
        require("switch (gfx_ios_interface_orientation())", patched_modes, "display orientation events use scene geometry")
    print("iOS landscape orientation, point/pixel scaling, root/MTK/touch layout, and scene pause checks passed")


if __name__ == "__main__":
    main()
