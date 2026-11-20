#!/usr/bin/env python3
"""Validate the branded iPhone app identity before packaging an IPA."""

from __future__ import annotations

import plistlib
import sys
from pathlib import Path


EXPECTED = {
    "CFBundleIdentifier": "com.larry.shitboxdev",
    "CFBundleDisplayName": "SHITBOX LABZ INC.",
    "CFBundleShortVersionString": "beta.069420INITIAL",
    "CFBundleVersion": "beta.069420INITIAL",
}


def validate(info_path: Path) -> None:
    with info_path.open("rb") as source:
        info = plistlib.load(source)
    differences = [
        f"{key}={info.get(key)!r} (expected {expected!r})"
        for key, expected in EXPECTED.items()
        if info.get(key) != expected
    ]
    if differences:
        raise ValueError("app identity mismatch: " + "; ".join(differences))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_app_identity.py <Info.plist>", file=sys.stderr)
        return 2
    try:
        validate(Path(sys.argv[1]))
    except (OSError, plistlib.InvalidFileException, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    print("SHITBOX LABZ INC. app identity validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
