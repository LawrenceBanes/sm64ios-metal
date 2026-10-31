#!/usr/bin/env python3
"""Read a SHA-256 digest file for the private game asset archive."""

from pathlib import Path
import re
import sys


def parse_checksum(path: Path, expected_name: str) -> str:
    lines = path.read_text(encoding="utf-8").splitlines()
    if len(lines) != 1:
        raise ValueError("checksum file must contain exactly one line")

    fields = lines[0].split(None, 1)
    if not fields:
        raise ValueError("checksum file is empty")
    digest = fields[0]
    if digest.startswith("\\"):
        digest = digest[1:]
    if not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
        raise ValueError("checksum file must start with a 64-character SHA-256 digest")

    if len(fields) == 2:
        filename = fields[1].strip()
        if filename.startswith("*"):
            filename = filename[1:]
        if Path(filename).name != expected_name:
            raise ValueError(f"checksum filename must identify {expected_name}")

    return digest.lower()


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: asset_checksum.py CHECKSUM_FILE EXPECTED_ARCHIVE_NAME")
    try:
        print(parse_checksum(Path(sys.argv[1]), sys.argv[2]))
    except (OSError, UnicodeError, ValueError) as error:
        raise SystemExit(f"invalid asset checksum file: {error}") from error


if __name__ == "__main__":
    main()
