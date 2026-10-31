#!/usr/bin/env python3
"""Backport scene-based orientation queries into the pinned SDL2 UIKit sources."""

from pathlib import Path
import sys


def insert_declaration(source: str, include: str) -> str:
    declaration = "extern UIInterfaceOrientation gfx_ios_interface_orientation(void);"
    if declaration in source:
        return source
    if include not in source:
        raise SystemExit(f"pinned SDL source is missing expected include: {include}")
    return source.replace(include, f"{include}\n\n{declaration}", 1)


def patch_file(path: Path, include: str, old: str, replacement: str, minimum: int = 1) -> str:
    source = path.read_text()
    count = source.count(old)
    if count < minimum:
        raise SystemExit(f"{path} contains {count} orientation query matches; expected at least {minimum}")
    source = source.replace(old, replacement)
    return insert_declaration(source, include)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_sdl2_ios27_orientation.py SDL2_SOURCE_DIR")
    source_root = Path(sys.argv[1]) / "src/video/uikit"
    video = source_root / "SDL_uikitvideo.m"
    modes = source_root / "SDL_uikitmodes.m"

    video_source = patch_file(
        video,
        '#include "SDL_uikitvideo.h"',
        "UIInterfaceOrientation orient = [UIApplication sharedApplication].statusBarOrientation;",
        "UIInterfaceOrientation orient = gfx_ios_interface_orientation();",
    )
    if "UIInterfaceOrientation orient = gfx_ios_interface_orientation();" not in video_source:
        raise SystemExit("SDL fullscreen view sizing does not use the scene orientation helper")
    if "if (orient == UIInterfaceOrientationUnknown) return frame;" not in video_source:
        marker = "UIInterfaceOrientation orient = gfx_ios_interface_orientation();"
        video_source = video_source.replace(marker, marker + "\n    if (orient == UIInterfaceOrientationUnknown) return frame;", 1)
    video.write_text(video_source)

    modes_source = modes.read_text()
    original_orientation_query = "[UIApplication sharedApplication].statusBarOrientation"
    mode_query_count = modes_source.count(original_orientation_query)
    if mode_query_count < 2:
        raise SystemExit(f"{modes} contains {mode_query_count} status-bar orientation queries; expected display mode and orientation event queries")
    modes_source = modes_source.replace(original_orientation_query, "gfx_ios_interface_orientation()")
    modes_source = insert_declaration(modes_source, '#include "SDL_uikitmodes.h"')
    if "statusBarOrientation" in modes_source:
        raise SystemExit("SDL display-mode orientation still reads deprecated statusBarOrientation")
    if modes_source.count("gfx_ios_interface_orientation()") < mode_query_count:
        raise SystemExit("SDL display-mode orientation replacements are incomplete")
    modes.write_text(modes_source)

    print("Patched pinned SDL UIKit sizing and display orientation to use UIWindowScene effective geometry on iOS 27")


if __name__ == "__main__":
    main()
