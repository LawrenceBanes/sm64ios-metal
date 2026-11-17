#!/usr/bin/env python3
"""Stage only the runtime files produced by the SM64 iOS game build."""

import argparse
from pathlib import Path
import shutil


def _copy_required_file(source: Path, destination: Path, label: str) -> None:
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"required {label} is missing or not a regular file: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def stage_runtime_payload(build_dir: Path, app_dir: Path, external_data: bool = False) -> list[str]:
    build_dir = build_dir.resolve(strict=True)
    app_dir = app_dir.resolve(strict=True)
    if not build_dir.is_dir():
        raise ValueError(f"game build path is not a directory: {build_dir}")
    if not app_dir.is_dir() or app_dir.suffix != ".app":
        raise ValueError(f"expected an existing .app directory, got {app_dir}")

    staged = []
    _copy_required_file(build_dir / "sm64ios", app_dir / "sm64ios", "game executable")
    staged.append("sm64ios")

    if external_data:
        relative_pack = Path("res") / "base.zip"
        _copy_required_file(build_dir / relative_pack, app_dir / relative_pack, "external game resource pack")
        staged.append(relative_pack.as_posix())

    return staged


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("app_dir", type=Path)
    parser.add_argument("--external-data", action="store_true", help="include the runtime res/base.zip pack")
    args = parser.parse_args()
    staged = stage_runtime_payload(args.build_dir, args.app_dir, args.external_data)
    print("Staged iOS runtime files: " + ", ".join(staged))


if __name__ == "__main__":
    main()
