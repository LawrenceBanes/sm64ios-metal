#!/usr/bin/env python3
"""Package one built .app directory into an unsigned Payload-style IPA."""

import argparse
import os
from pathlib import Path
import stat
import shutil
import subprocess
import sys
import tempfile
import zipfile


def package_app(app_path: Path, ipa_path: Path) -> None:
    app = app_path.resolve(strict=True)
    if not app.is_dir() or app.suffix != ".app":
        raise ValueError(f"expected an existing .app bundle, got {app}")
    if not (app / "Info.plist").is_file():
        raise ValueError("app bundle has no Info.plist")
    destination = ipa_path.resolve()
    if destination == app or app in destination.parents:
        raise ValueError("IPA output must be outside the app bundle")
    destination.parent.mkdir(parents=True, exist_ok=True)
    ditto = Path("/usr/bin/ditto")
    if sys.platform == "darwin" and ditto.is_file():
        # Use Apple's archive writer for release IPAs. It preserves directory
        # records, file metadata, and resource forks in the conventional
        # Payload/<app>.app structure expected by iOS installers.
        with tempfile.TemporaryDirectory(prefix="sm64-ipa-package-", dir=destination.parent) as temp_dir:
            staging = Path(temp_dir)
            payload = staging / "Payload"
            payload.mkdir()
            shutil.copytree(app, payload / app.name, symlinks=True, copy_function=shutil.copy2)
            temporary_ipa = staging / "package.ipa"
            subprocess.run(
                [str(ditto), "-c", "-k", "--sequesterRsrc", "--keepParent", "Payload", str(temporary_ipa)],
                cwd=staging,
                check=True,
            )
            temporary_ipa.replace(destination)
        print(f"created unsigned IPA with Apple ditto: {destination}")
        return

    with tempfile.NamedTemporaryFile(prefix="sm64-ipa-", suffix=".ipa", dir=destination.parent, delete=False) as temp:
        temporary_ipa = Path(temp.name)
    try:
        with zipfile.ZipFile(temporary_ipa, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            def add_directory(path: Path, archive_name: str) -> None:
                info = zipfile.ZipInfo(f"{archive_name}/", date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = (stat.S_IFDIR | stat.S_IMODE(path.stat().st_mode)) << 16 | 0x10
                archive.writestr(info, b"")

            add_directory(app.parent, "Payload")
            add_directory(app, f"Payload/{app.name}")
            for path in sorted(app.rglob("*")):
                relative = path.relative_to(app)
                if path.is_dir() and not path.is_symlink():
                    add_directory(path, f"Payload/{app.name}/{relative.as_posix()}")
                    continue
                if path.is_symlink():
                    resolved = path.resolve(strict=True)
                    if app not in resolved.parents:
                        raise ValueError(f"app bundle symlink escapes bundle: {relative}")
                    data = os.fsencode(os.readlink(path))
                    file_type = stat.S_IFLNK
                elif path.is_file():
                    data = path.read_bytes()
                    file_type = stat.S_IFREG
                else:
                    raise ValueError(f"unsupported special file in app bundle: {relative}")
                info = zipfile.ZipInfo(f"Payload/{app.name}/{relative.as_posix()}", date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (file_type | stat.S_IMODE(path.lstat().st_mode)) << 16
                archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
        temporary_ipa.replace(destination)
    except Exception:
        temporary_ipa.unlink(missing_ok=True)
        raise
    print(f"created unsigned IPA: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("ipa", type=Path)
    args = parser.parse_args()
    package_app(args.app, args.ipa)


if __name__ == "__main__":
    main()
