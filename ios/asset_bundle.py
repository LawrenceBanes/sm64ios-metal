#!/usr/bin/env python3
"""Create and safely unpack a private archive of extracted SM64 build assets."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile


ASSET_DIRS = {"actors", "levels", "textures", "sound", "assets"}
MANIFEST = ".assets-local.txt"


def asset_paths(root: Path) -> tuple[set[str], set[str]]:
    asset_map = json.loads((root / "assets.json").read_text())
    all_assets = {name for name in asset_map if not name.startswith("@")}
    us_assets = {name for name, data in asset_map.items() if not name.startswith("@") and "us" in data[-1]}
    return all_assets, us_assets


def read_local_manifest(data: bytes) -> set[str]:
    lines = data.decode("utf-8").splitlines()
    if len(lines) < 2 or not lines[0].startswith("#") or lines[1] != "6":
        raise ValueError("unsupported or missing extracted-asset manifest")
    paths = set(lines[2:])
    if any(not p for p in paths) or len(paths) != len(lines[2:]):
        raise ValueError("asset manifest contains empty or duplicate paths")
    return paths


def validate_name(name: str, allowed: set[str]) -> None:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError(f"unsafe archive path: {name!r}")
    if name != MANIFEST and (path.parts[0] not in ASSET_DIRS or name not in allowed):
        raise ValueError(f"path is not a mapped game asset: {name!r}")


def asset_path(root: Path, name: str, allow_final_symlink: bool = False) -> Path:
    path = root
    parts = PurePosixPath(name).parts
    for index, part in enumerate(parts):
        path = path / part
        if path.is_symlink():
            if allow_final_symlink and index == len(parts) - 1:
                return path
            raise ValueError(f"asset path crosses a symlink: {name!r}")
        if not path.resolve().is_relative_to(root):
            raise ValueError(f"asset path escapes build root: {name!r}")
    return path


def create_bundle(root: Path, output: Path) -> None:
    root = root.resolve()
    mapped, us_assets = asset_paths(root)
    manifest = root / MANIFEST
    local = read_local_manifest(manifest.read_bytes())
    if local != mapped:
        raise ValueError("local extracted-asset manifest does not match assets.json")
    paths = [MANIFEST, *sorted(us_assets)]
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w:gz", compresslevel=6) as archive:
        for name in paths:
            validate_name(name, mapped)
            path = asset_path(root, name)
            if not path.is_file() or path.is_symlink():
                raise ValueError(f"missing or non-regular extracted asset: {name}")
            archive.add(path, arcname=name, recursive=False)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"asset bundle: {output}")
    print(f"sha256: {digest}")
    print(f"USA game asset files: {len(us_assets)} (ROM excluded)")


def extract_bundle(root: Path, bundle: Path) -> None:
    root = root.resolve()
    mapped, us_assets = asset_paths(root)
    with tarfile.open(bundle, "r:gz") as archive:
        members = archive.getmembers()
        by_name = {member.name: member for member in members}
        if len(by_name) != len(members):
            raise ValueError("asset bundle contains duplicate paths")
        if MANIFEST not in by_name or not by_name[MANIFEST].isfile():
            raise ValueError("asset bundle has no regular extracted-asset manifest")
        local_data = archive.extractfile(by_name[MANIFEST]).read()
        local = read_local_manifest(local_data)
        if local != mapped:
            raise ValueError("asset bundle manifest does not match this checkout's assets.json")
        if set(by_name) != us_assets | {MANIFEST}:
            raise ValueError("asset bundle must contain exactly the USA-mapped files and manifest")
        total_bytes = 0
        for member in members:
            validate_name(member.name, mapped)
            if not member.isfile():
                raise ValueError(f"non-regular file in asset bundle: {member.name}")
            if member.size < 0:
                raise ValueError(f"negative file size in asset bundle: {member.name}")
            total_bytes += member.size
            if total_bytes > 2_000_000_000:
                raise ValueError("asset bundle exceeds the 2 GB unpacked safety limit")
        for member in members:
            destination = asset_path(root, member.name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, destination.open("wb") as target:
                while chunk := source.read(1024 * 1024):
                    target.write(chunk)
    print(f"extracted {len(us_assets)} USA-mapped assets from private bundle")


def clean_assets(root: Path) -> None:
    root = root.resolve()
    mapped, us_assets = asset_paths(root)
    for name in sorted(us_assets, key=lambda item: item.count("/"), reverse=True):
        validate_name(name, mapped)
        path = asset_path(root, name, allow_final_symlink=True)
        if path.is_symlink() or path.is_file():
            path.unlink()
        parent = path.parent
        while parent != root and root in parent.parents:
            try:
                parent.rmdir()
            except OSError:
                break
            parent = parent.parent
    (root / MANIFEST).unlink(missing_ok=True)
    print(f"removed {len(us_assets)} USA-mapped asset paths from the build checkout")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("create", "extract", "clean"))
    parser.add_argument("path", type=Path, nargs="?", help="output path for create; bundle path for extract")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    if args.command == "create":
        if args.path is None:
            parser.error("create requires an output path")
        create_bundle(args.root, args.path)
    elif args.command == "extract":
        if args.path is None:
            parser.error("extract requires a bundle path")
        extract_bundle(args.root, args.path)
    else:
        if args.path is not None:
            parser.error("clean takes no path argument")
        clean_assets(args.root)


if __name__ == "__main__":
    main()
